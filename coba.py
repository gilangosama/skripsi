import json
import time
import requests
import numpy as np
import paho.mqtt.client as mqtt
from tensorflow.keras.models import load_model
from datetime import datetime

# Load model RNN Anda
MODEL_PATH = "model_rnn.h5"
model = load_model(MODEL_PATH)

# MQTT Configuration
BROKER = "test.mosquitto.org"
PORT = 1883
TOPIC_SENSOR = "irigasi/sensor_data"
TOPIC_PREDICTION = "irigasi/prediction"
TOPIC_MONITOR = "irigasi/monitor"

# API Visual Crossing Configuration
API_KEY = '4QZNYXPF45CJ5YYU8DLCJ8REF'
LAT = -6.9237
LON = 106.928726
BASE_URL = "https://weather.visualcrossing.com/VisualCrossingWebServices/rest/services/timeline"

data_received = {}
last_prediction_time = None

def get_visualcrossing_data():
    """
    Fetch precipitation and wind gust data from Visual Crossing API.
    """
    url = f"{BASE_URL}/{LAT},{LON}/today?unitGroup=metric&include=days&key={API_KEY}&contentType=json"
    try:
        response = requests.get(url)
        if response.status_code == 200:
            weather_data = response.json()
            today_data = weather_data['days'][0]
            return {
                "precip": today_data.get("precip", 0),
                "windgust": today_data.get("windgust", 0)
            }
        else:
            print(f"Failed to fetch data from API, status code: {response.status_code}")
    except Exception as e:
        print(f"Error fetching data from Visual Crossing: {e}")
    return {"precip": 0, "windgust": 0}

def preprocess_data(data):
    """
    Preprocess data from MQTT and API for model input.
    Expected input: {"temp": 25, "humidity": 60, "precip": 0.1, "windgust": 5}
    """
    features = ["temp", "humidity", "precip", "windgust"]
    input_data = [data.get(feature, 0) for feature in features]
    return np.array(input_data).reshape(1, -1)

def predict_weather(data):
    """
    Predict weather using the RNN model.
    """
    processed_data = preprocess_data(data)
    prediction = model.predict(processed_data)
    predicted_class = np.argmax(prediction, axis=1)[0]
    classes = ["Clear", "Partially cloudy", "Rain, Overcast", "Rain, Partially cloudy"]
    return classes[predicted_class]

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("Connected to MQTT Broker!")
        client.subscribe(TOPIC_SENSOR)
    else:
        print(f"Failed to connect, return code {rc}")

def on_message(client, userdata, msg):
    global data_received
    try:
        payload = json.loads(msg.payload.decode())
        print(f"Received data: {payload}")
        data_received = payload
    except json.JSONDecodeError:
        print("Failed to decode JSON")

def should_send_prediction():
    """
    Check if the current time matches the prediction schedule (08:15 or 15:15).
    """
    current_time = datetime.now()
    global last_prediction_time

    # Check if it's 08:15 or 15:15 and not already sent for this time
    if current_time.hour == 8 and current_time.minute == 15:
        if last_prediction_time != "8:15":
            last_prediction_time = "8:15"
            return True
    elif current_time.hour == 8 and current_time.minute == 20:
        if last_prediction_time != "8:20":
            last_prediction_time = "8:20"
            return True

    return False

def main():
    global data_received

    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message

    client.connect(BROKER, PORT, 60)
    client.loop_start()

    while True:
        # Real-time monitoring
        if data_received:
            monitor_payload = {
                "temperature": data_received.get("temperature", 0),
                "humidity": data_received.get("humidity", 0),
                "soil_moisture": data_received.get("soil_moisture", 0)
            }
            client.publish(TOPIC_MONITOR, json.dumps(monitor_payload))
            print(f"Published monitor data: {monitor_payload}")

        # Scheduled predictions
        if should_send_prediction():
            visualcrossing_data = get_visualcrossing_data()

            # Combine sensor data with API data
            combined_data = {
                "temperature": data_received.get("temperature", 0),
                "humidity": data_received.get("humidity", 0),
                **visualcrossing_data
            }

            # Perform prediction
            prediction_today = predict_weather(combined_data)

            # Simulate tomorrow's prediction (in real case, you might use future data)
            prediction_tomorrow = predict_weather(combined_data)

            # Publish predictions
            prediction_payload = {
                "prediction_today": prediction_today,
                "prediction_tomorrow": prediction_tomorrow
            }
            client.publish(TOPIC_PREDICTION, json.dumps(prediction_payload))
            print(f"Published prediction: {prediction_payload}")

        time.sleep(10)  # Wait 10 seconds before next iteration

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("Exiting program.")
