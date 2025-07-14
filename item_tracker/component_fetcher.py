"""Component Fetcher
--------------------

A helper script to read location codes from the clipboard, look up
coordinates from an Excel spreadsheet and send them to an ESP32 over
serial.  The GUI shows the queued locations and lets you step through
with a button or using a "NEXT" message from the board.

Before running, make sure to:
- Set SERIAL_PORT to match your serial device (e.g. '/dev/ttyUSB0' or 'COM3').
- Point EXCEL_PATH to the correct path of your 'locations.xlsx' file.
- Install dependencies with:
  pip install pandas pyperclip PySimpleGUI pyserial
"""

import re
import sys
import threading
import time
from pathlib import Path

import pandas as pd
import pyperclip
import PySimpleGUI as sg
import serial

# -------------------------- Configuration --------------------------
SERIAL_PORT = '/dev/ttyUSB0'  # Adjust for your system
BAUD_RATE = 115200
EXCEL_PATH = Path('locations.xlsx')  # Update path if needed
EXCEL_SHEET = 'Sheet1'
POLL_INTERVAL = 0.5  # seconds

# -------------------------------------------------------------------

def load_locations(path: Path, sheet: str) -> pd.DataFrame:
    df = pd.read_excel(path, sheet_name=sheet)
    expected_cols = ['L', 'R', 'C', 'X_CM', 'Y_CM']
    missing = [c for c in expected_cols if c not in df.columns]
    if missing:
        raise ValueError(f"Missing columns in Excel file: {missing}")
    df = df[expected_cols]
    return df


def parse_locations(text: str):
    """Return a list of dicts with keys L, R, C from text."""
    cleaned = re.sub(r'[\s\-()]+', '', text.upper())
    tokens = re.findall(r'[LRC]\d{1,3}', cleaned)

    groups = []
    current = {}
    for token in tokens:
        key = token[0]
        value = int(token[1:])
        current[key] = value
        if set(current.keys()) == {'L', 'R', 'C'}:
            groups.append({'L': current['L'], 'R': current['R'], 'C': current['C']})
            current = {}
    return groups


def clipboard_poll(df, location_buffer, window):
    last_text = None
    while True:
        text = None
        try:
            text = pyperclip.paste()
        except pyperclip.PyperclipException:
            pass
        if text and text != last_text:
            last_text = text
            for loc in parse_locations(text):
                row = df[(df.L == loc['L']) & (df.R == loc['R']) & (df.C == loc['C'])]
                if not row.empty:
                    x_cm = row.X_CM.iloc[0]
                    y_cm = row.Y_CM.iloc[0]
                    label = f"L{loc['L']:02d}-R{loc['R']:02d}-C{loc['C']:02d}"
                    entry = (label, x_cm, y_cm)
                    if entry not in location_buffer:
                        location_buffer.append(entry)
                        window.write_event_value('UPDATE_LIST', list(location_buffer))
        time.sleep(POLL_INTERVAL)


def main():
    df = load_locations(EXCEL_PATH, EXCEL_SHEET)
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)

    location_buffer = []

    layout = [
        [sg.Listbox(values=[], size=(30, 10), key='-LIST-')],
        [sg.Button('Next', key='-NEXT-'), sg.Button('Exit')]
    ]
    window = sg.Window('Component Fetcher', layout, finalize=True)

    poll_thread = threading.Thread(target=clipboard_poll, args=(df, location_buffer, window), daemon=True)
    poll_thread.start()

    while True:
        event, _ = window.read(timeout=100)
        if event == sg.WIN_CLOSED or event == 'Exit':
            break
        if event == 'UPDATE_LIST':
            window['-LIST-'].update(values=[e[0] for e in location_buffer])
            window.un_hide()
        try:
            line = ser.readline().decode('utf-8')
        except Exception:
            line = ''
        if event == '-NEXT-' or line == 'NEXT\n':
            if location_buffer:
                label, x_cm, y_cm = location_buffer.pop(0)
                msg = f"X:{x_cm:.2f},Y:{y_cm:.2f}\n"
                ser.write(msg.encode('utf-8'))
                window['-LIST-'].update(values=[e[0] for e in location_buffer])
            if not location_buffer:
                window.close()
                sys.exit()

    window.close()
    ser.close()


if __name__ == '__main__':
    main()
