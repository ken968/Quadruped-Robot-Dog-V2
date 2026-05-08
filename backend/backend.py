import subprocess
import time
import sys
import os

def start_services():
    print('...Start the QuadRoboDog\n')
    current_dir = os.path.dirname(os.path.abspath(__file__))
    dashboard_path = os.path.join(current_dir, 'dashboard.py')
    movement_path = os.path.join(current_dir, 'movement.py')

    try:
        print('...Dashboard Controll\n')
        dashboard_process = subprocess.Popen([sys.executable, dashboard_path])
        time.sleep(2)

        print('...Activating process')
        movement_process = subprocess.Popen([sys.executable, movement_path])

        dashboard_process.wait()
        movement_process.wait()

    except KeyboardInterrupt:
        print('...Powering Off \n')
        dashboard_process.terminate()
        movement_process.terminate()
        print('...Shutdown')

if __name__ == '__main__':
    start_services()