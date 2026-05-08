import paho.mqtt.client as mqtt
import json
import time
import os

json_path = os.path.join(os.path.dirname(__file__), '..', 'commands.json')

with open(json_path, 'r') as f:
    COMMAND = json.load(f)

BROKER = "localhost"
PORT = 1883
TOPIC_CMD = "robodog/cmd"

def on_connect(client, userdata, flags, rc, properties=None):
    print(f"...connected {rc}")

client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
client.on_connect = on_connect
client.connect(BROKER, PORT, 60)
client.loop_start()

try:
    print('gerak\n')
    time.sleep(1)

    print('idle')
    payload = COMMAND["IDLE"]
    client.publish(TOPIC_CMD, json.dumps(payload))
    time.sleep(3)

    print('kanan')
    payload = COMMAND["KANAN"]
    client.publish(TOPIC_CMD, json.dumps(payload))
    time.sleep(5)

    print('kiri')
    payload = COMMAND["KIRI"]
    client.publish(TOPIC_CMD, json.dumps(payload))
    time.sleep(3)

    print('maju')
    payload = COMMAND["MAJU"]
    client.publish(TOPIC_CMD, json.dumps(payload))
    time.sleep(5)

    print('mundur')
    payload = COMMAND["MUNDUR"]
    client.publish(TOPIC_CMD, json.dumps(payload))
    time.sleep(3)

    print('duduk')
    payload = COMMAND["DUDUK"]
    client.publish(TOPIC_CMD, json.dumps(payload))
    time.sleep(5)

    print('tidur')
    payload = COMMAND["TIDUR"]
    client.publish(TOPIC_CMD, json.dumps(payload))
    time.sleep(3)

    print('manual')
    payload = COMMAND["MANUAL"]
    client.publish(TOPIC_CMD, json.dumps(payload))
    time.sleep(5)

    print('autoPilot')
    payload = COMMAND["AUTOPILOT"]
    client.publish(TOPIC_CMD, json.dumps(payload))
    time.sleep(3)

    print('berhenti')
    payload = COMMAND["BERHENTI"]
    client.publish(TOPIC_CMD, json.dumps(payload))
    time.sleep(5)

    print('led')
    payload = COMMAND["LED"]
    client.publish(TOPIC_CMD, json.dumps(payload))
    time.sleep(3)

    while True:
        time.sleep(3)

except KeyboardInterrupt:
    print("...Out")
    client.loop_stop()
    client.disconnect()