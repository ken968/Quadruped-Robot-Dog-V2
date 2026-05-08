import paho.mqtt.client as mqtt
import json
import time
import os

json_path = os.path.join(os.path.dirname(__file__), 'config', 'commands.json')

with open(json_path, 'r') as f:
    COMMAND = json.load(f)

BROKER = "localhost"
TOPIC_CMD = "robodog/cmd"

client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)

def connect_mqtt():
    print(f'...emqx connect {BROKER}')
    client.connect(BROKER, 1883, 60)
    client.loop_start()
    return client