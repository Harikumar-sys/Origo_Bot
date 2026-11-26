import socket
import time
import cv2
import mediapipe as mp

# ---- ESP8266 AP Network Configuration ----
ESP_IP = "192.168.4.1"
ESP_PORT = 81
EMOTION_HOLD_SEC = 2.0  # Minimum seconds before changing to a different emotion

print(f"Connecting to robot at {ESP_IP}:{ESP_PORT}...")
try:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((ESP_IP, ESP_PORT))
    print("Connected successfully to the robot!")
except Exception as e:
    print(f"Connection failed: {e}")
    print("Make sure your PC is connected to the 'Dasai_Robot' Wi-Fi hotspot.")
    exit()

# ---- MediaPipe Hands & Face Mesh Initialization ----
mp_hands = mp.solutions.hands
hands = mp_hands.Hands(
    max_num_hands=1, min_detection_confidence=0.7, min_tracking_confidence=0.7
)
mp_draw = mp.solutions.drawing_utils

mp_face_mesh = mp.solutions.face_mesh
face_mesh = mp_face_mesh.FaceMesh(
    max_num_faces=1,
    refine_landmarks=True,
    min_detection_confidence=0.6,
    min_tracking_confidence=0.6,
)

# Initialize Webcam
cap = cv2.VideoCapture(0)

gesture_names = {0: "FORWARD", 1: "BACKWARD", 2: "LEFT", 3: "RIGHT", 4: "STOP"}
last_sent_gesture = -1

active_emotion = "NORMAL"
last_emotion_change_time = 0.0
emotion_sent_once = False


def send_gesture(index):
    global last_sent_gesture
    if index != last_sent_gesture:
        try:
            message = f"{index}\n"
            sock.sendall(message.encode("utf-8"))
            print(f"Sent Gesture Index: {index} ({gesture_names.get(index)})")
            last_sent_gesture = index
        except Exception as e:
            print(f"Error sending gesture: {e}")


def send_emotion(emotion):
    try:
        message = f"E:{emotion}\n"
        sock.sendall(message.encode("utf-8"))
        print(f"Sent Emotion: {emotion}")
    except Exception as e:
        print(f"Error sending emotion: {e}")


def detect_emotion(rgb_frame, frame_w, frame_h):
    """Return 'HAPPY', 'SAD', or 'NORMAL' using lip corner & mouth-width ratios."""
    res = face_mesh.process(rgb_frame)
    if not res.multi_face_landmarks:
        return "NORMAL"

    lm = res.multi_face_landmarks[0].landmark

    # Key landmarks:
    # 61 = left mouth corner, 291 = right mouth corner
    # 13 = upper inner lip, 14 = lower inner lip
    # 234 = left cheek/face edge, 454 = right cheek/face edge
    lx, ly = lm[61].x * frame_w, lm[61].y * frame_h
    rx, ry = lm[291].x * frame_w, lm[291].y * frame_h
    ux, uy = lm[13].x * frame_w, lm[13].y * frame_h
    dx, dy = lm[14].x * frame_w, lm[14].y * frame_h

    face_width = max(1.0, abs(lm[454].x - lm[234].x) * frame_w)
    mouth_width = ((rx - lx) ** 2 + (ry - ly) ** 2) ** 0.5
    mouth_center_y = 0.5 * (uy + dy)
    corners_avg_y = 0.5 * (ly + ry)

    # Normalized metrics relative to face width
    width_ratio = mouth_width / face_width
    # Positive means corners sit higher (smaller y) than mouth center -> smile
    corner_lift = (mouth_center_y - corners_avg_y) / face_width

    if width_ratio > 0.39 and corner_lift > 0.008:
        return "HAPPY"
    elif corner_lift < -0.018 and width_ratio < 0.37:
        return "SAD"
    return "NORMAL"


while cap.isOpened():
    success, frame = cap.read()
    if not success:
        print("Ignoring empty camera frame.")
        continue

    frame = cv2.flip(frame, 1)
    h, w, _ = frame.shape
    rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

    # 1. Hand gesture detection -> motion (0..4)
    results = hands.process(rgb_frame)
    current_gesture = 4  # Default to STOP

    if results.multi_hand_landmarks:
        for hand_landmarks in results.multi_hand_landmarks:
            mp_draw.draw_landmarks(frame, hand_landmarks, mp_hands.HAND_CONNECTIONS)
            landmarks = hand_landmarks.landmark

            tips_ids = [8, 12, 16, 20]
            fingers = []
            for tip in tips_ids:
                if landmarks[tip].y < landmarks[tip - 2].y:
                    fingers.append(1)
                else:
                    fingers.append(0)

            total_fingers = sum(fingers)
            if total_fingers == 1:
                current_gesture = 0  # Forward
            elif total_fingers == 2:
                current_gesture = 1  # Backward
            elif total_fingers == 3:
                current_gesture = 2  # Left
            elif total_fingers == 4:
                current_gesture = 3  # Right
            else:
                current_gesture = 4  # Stop

    send_gesture(current_gesture)

    # 2. Face expression detection with 5-second hold before switching
    detected_emotion = detect_emotion(rgb_frame, w, h)
    now = time.monotonic()

    if not emotion_sent_once:
        active_emotion = detected_emotion
        last_emotion_change_time = now
        send_emotion(active_emotion)
        emotion_sent_once = True
    elif detected_emotion != active_emotion:
        if (now - last_emotion_change_time) >= EMOTION_HOLD_SEC:
            active_emotion = detected_emotion
            last_emotion_change_time = now
            send_emotion(active_emotion)

    remaining_hold = max(0.0, EMOTION_HOLD_SEC - (now - last_emotion_change_time))

    cv2.putText(
        frame,
        f"Command: {gesture_names.get(current_gesture)}",
        (20, 40),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.85,
        (0, 255, 0),
        2,
        cv2.LINE_AA,
    )
    cv2.putText(
        frame,
        f"OLED: {active_emotion} (Saw: {detected_emotion})",
        (20, 78),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.8,
        (0, 212, 255),
        2,
        cv2.LINE_AA,
    )
    cv2.putText(
        frame,
        f"Hold timer: {remaining_hold:.1f}s",
        (20, 114),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.75,
        (200, 200, 200),
        2,
        cv2.LINE_AA,
    )

    cv2.imshow("Robot Gesture & Emotion Control", frame)

    if cv2.waitKey(1) & 0xFF == ord("q"):
        break

cap.release()
cv2.destroyAllWindows()
sock.close()