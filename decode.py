#!/usr/bin/env python3
import sys
import __future__

HEADER_BOOT =  0x00
HEADER_UPDATE = 0x01
HEADER_BUTTON_CLICK = 0x02
HEADER_BUTTON_HOLD  = 0x03

header_lut = {
    HEADER_BOOT: 'BOOT',
    HEADER_UPDATE: 'UPDATE',
    HEADER_BUTTON_CLICK: 'BUTTON_CLICK',
    HEADER_BUTTON_HOLD: 'BUTTON_HOLD'
}


def decode(data):
    if len(data) != 32:
        raise Exception("Bad data length, 32 characters expected")

    header = int(data[0:2], 16)

    print(data[8:10])

    temperature = int(data[4:8], 16) if data[6:10] != 'ffff' else None

    if temperature:
        if temperature > 32768:
            temperature -= 65536
        temperature /= 10.0

    return {
        "header": header_lut[header],
        "voltage": int(data[2:4], 16) / 10.0 if data[2:4] != 'ff' else None,
        "temperature": temperature,
        "humidity": int(data[8:10], 16) / 2.0 if data[8:10] != 'ff' else None,
        "pressure": int(data[14:18], 16) * 2 if data[14:18] != 'ffff' else None,
        "voc": int(data[10:14], 16) if data[10:14] != 'ffff' else None,
        "co2": int(data[18:22], 16) if data[18:22] != 'ffff' else None
    }


def pprint(data):
    print('Header :', data['header'])
    print('Voltage :', data['voltage'])
    print('Temperature :', data['temperature'])
    print('Humidity :', data['humidity'])
    print('Pressure :', data['pressure'])
    print('VOC :', data['voc'])
    print('CO2 :', data['co2'])


if __name__ == '__main__':
    if len(sys.argv) != 2 or sys.argv[1] in ('help', '-h', '--help'):
        print("usage: python3 decode.py [data]")
        print("example: python3 decode.py 001E0100F5540070C1BE00000001FFFF")
        exit(1)

    data = decode(sys.argv[1].lower())
    pprint(data)
