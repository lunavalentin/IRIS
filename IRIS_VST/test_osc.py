import argparse
import time
from pythonosc import udp_client

def send_listener_pos(client, x, y):
    print(f"Sending /listener/pos {x} {y}")
    client.send_message("/listener/pos", [float(x), float(y)])

def send_ir_pos(client, index, x, y):
    print(f"Sending /ir/pos {index} {x} {y}")
    client.send_message("/ir/pos", [int(index), float(x), float(y)])

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--ip", default="127.0.0.1", help="The ip of the OSC server")
    parser.add_argument("--port", type=int, default=9001, help="The port the OSC server is listening on")
    args = parser.parse_args()

    client = udp_client.SimpleUDPClient(args.ip, args.port)

    print("Testing IRIS V3 OSC Input...")
    
    # 1. Move Listener in a circle
    import math
    for i in range(20):
        angle = i * 0.3
        x = 0.5 + 0.3 * math.cos(angle)
        y = 0.5 + 0.3 * math.sin(angle)
        send_listener_pos(client, x, y)
        time.sleep(0.1)
        
    # 2. Move IR 0
    print("Moving IR 0...")
    send_ir_pos(client, 0, 0.2, 0.2)
    time.sleep(0.5)
    send_ir_pos(client, 0, 0.8, 0.8)
    
    print("Done.")
