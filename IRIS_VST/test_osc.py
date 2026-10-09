"""Send test OSC messages to IRIS4 (V4 protocol) and optionally print what it sends.

    pip install python-osc
    python test_osc.py --listen                 # print messages IRIS sends to 127.0.0.1:9002
    python test_osc.py --uuid <listener-uuid>   # move that listener in a circle, sweep Spread
"""
import argparse
import math
import threading
import time

from pythonosc import dispatcher, osc_server, udp_client


def listen(port):
    d = dispatcher.Dispatcher()
    d.set_default_handler(lambda addr, *args: print(addr, args))
    server = osc_server.ThreadingOSCUDPServer(("127.0.0.1", port), d)
    print(f"Listening for IRIS output on 127.0.0.1:{port} (Ctrl-C to stop)")
    server.serve_forever()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--ip", default="127.0.0.1", help="host running IRIS")
    parser.add_argument("--port", type=int, default=9001, help="IRIS receive port")
    parser.add_argument("--uuid", help="listener UUID to move (see --listen output)")
    parser.add_argument("--name", default="A", help="listener name to send with the position")
    parser.add_argument("--listen", action="store_true", help="print messages sent by IRIS on port 9002")
    args = parser.parse_args()

    if args.listen:
        if args.uuid:
            threading.Thread(target=listen, args=(9002,), daemon=True).start()
        else:
            listen(9002)

    if args.uuid:
        client = udp_client.SimpleUDPClient(args.ip, args.port)
        print("Moving listener in a circle and sweeping Spread (Ctrl-C to stop)")
        for i in range(400):
            angle = i * 0.05
            x = 0.5 + 0.3 * math.cos(angle)
            y = 0.5 + 0.3 * math.sin(angle)
            client.send_message("/iris/listener/sync", [args.uuid, args.name, float(x), float(y), 0, 0])
            client.send_message("/iris/param/spread", float(0.2 + 0.2 * (1 + math.sin(angle * 0.5))))
            time.sleep(0.05)
