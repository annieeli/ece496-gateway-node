import argparse
import serial
import time


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "-p",
        "--port",
        default="/dev/ttyUSB0",
        help="Provide serial port",
    )
    parser.add_argument(
        "-b",
        "--baud-rate",
        default=115200,
        help="Provide baud rate",
    )
    args = parser.parse_args()
    return args


def main():

    args = parse_args()

    try:
        ser = serial.Serial(args.port, args.baud_rate, timeout=1)
    except serial.SerialException:
        print("Could not open serial port provided...")
        exit(-1)

    print(f"Connected to {args.port} at {args.baud_rate} baud")

    try:
        while True:
            message = input("Message to send to ESP32: ")
            # Send something
            ser.write((message + "\n").encode())

            # Read response if any
            line = ser.readline().decode(errors="ignore").strip()
            if line:
                print("ESP32:", line)

            time.sleep(1)

    except KeyboardInterrupt:
        print("\nExiting...")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
