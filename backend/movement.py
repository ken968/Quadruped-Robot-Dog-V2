import os
import json
import paho.mqtt.client as mqtt
from pynput import keyboard

from mqtt_client import client, connect_mqtt, COMMAND, TOPIC_CMD
connect_mqtt()
current_key = None


def send_command(cmd_name):
    print(f'aksi: {cmd_name}\n')
    client.publish(TOPIC_CMD, json.dumps(COMMAND[cmd_name]))

def on_press(key):
    global current_key
    try:
        k = key.char.lower()

        if k == current_key:
            return

        current_key = k
        if k == 'w':
            send_command("MAJU")
        elif k == 'a':
            send_command("KIRI")
        elif k == 's':
            send_command("MUNDUR")
        elif k == 'd':
            send_command("KANAN")
        elif k == 'e':
            send_command("DUDUK")
        elif k == 'q':
            send_command("TIDUR")
        elif k == '1':
            send_command("LEDON")
        elif k == '0':
            send_command("LEDOFF")
        elif k == 'p':
            send_command("AUTOPILOT")
        elif k == 'm':
            send_command("MANUAL")

    except AttributeError:
        if key == keyboard.Key.space and current_key != 'space':
            current_key = 'space'
            send_command('BERHENTI')

def on_release(key):
    global current_key
    try:
        k = key.char.lower()
        if k == current_key:
            if k in ['w', 'a', 's', 'd', 'e', 'q']:
                send_command('BERHENTI')
            current_key = None

    except AttributeError:
        if key == keyboard.Key.space:
            current_key = None
        elif key == keyboard.Key.esc:
            print('...Powering Off \n')
            client.loop_stop()
            client.disconnect()
            print('...Shutdown')
            return False

print('Controller_V1')

with keyboard.Listener(on_press = on_press, on_release = on_release) as listener:
    listener.join()
