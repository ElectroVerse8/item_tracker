"""Component Fetcher
--------------------

Search for component locations using the built in search field, look up
coordinates from an Excel spreadsheet and send them to an ESP32 over
serial. The GUI shows the queued locations and lets you step through
with a button or using a "NEXT" message from the board. Press Enter in
the search field or click the Search button to queue locations.

Before running, make sure to:
- Point ``EXCEL_PATH`` to the correct path of your ``locations.xlsx`` file.
- Install dependencies with ``pip install pandas pyserial``.
You'll be asked which serial port to use when the script starts.
Tkinter is bundled with most Python installations and is used for the GUI.
"""

import re
import sys
import time
from pathlib import Path

import pandas as pd
import tkinter as tk
import tkinter.messagebox as messagebox
import serial
from serial.tools import list_ports

# -------------------------- Configuration --------------------------
BAUD_RATE = 115200
EXCEL_PATH = Path('locations.xlsx')  # Update path if needed
EXCEL_SHEET = 'Sheet1'

# -------------------------------------------------------------------


def select_serial_port() -> serial.Serial:
    """Prompt the user for a serial port and open it.

    A small Tkinter window lists available ports with a Refresh button.
    The dialog loops until a port is successfully opened, returning the
    ``serial.Serial`` instance.
    """

    def show_dialog() -> str:
        dlg = tk.Tk()
        dlg.title('Select Serial Port')

        ports = [p.device for p in list_ports.comports()]
        port_var = tk.StringVar(value=ports[0] if ports else '')

        option = tk.OptionMenu(dlg, port_var, *ports)
        option.pack(padx=10, pady=5)

        def refresh():
            new_ports = [p.device for p in list_ports.comports()]
            menu = option['menu']
            menu.delete(0, 'end')
            for p in new_ports:
                menu.add_command(label=p, command=lambda v=p: port_var.set(v))
            if new_ports:
                port_var.set(new_ports[0])
            else:
                port_var.set('')

        tk.Button(dlg, text='Refresh', command=refresh).pack(pady=2)

        selected = {'port': None}

        def confirm():
            selected['port'] = port_var.get()
            dlg.destroy()

        tk.Button(dlg, text='OK', command=confirm).pack(pady=5)
        dlg.mainloop()
        return selected['port']

    while True:
        port = show_dialog()
        if not port:
            continue
        try:
            ser = serial.Serial(port, BAUD_RATE, timeout=0.1)
            return ser
        except Exception as exc:
            messagebox.showerror('Connection Error', f'Failed to open {port}: {exc}')


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
    # Match "L00012" style tokens while stripping leading zeros from the
    # numeric portion. This ensures codes like "L0000010" are treated the
    # same as "L10" while preserving zeros that appear in the middle of the
    # number.
    tokens = re.findall(r'([LRC])0*(\d+)', cleaned)

    groups = []
    current = {}
    for key, digits in tokens:
        value = int(digits)
        current[key] = value
        if set(current.keys()) == {'L', 'R', 'C'}:
            groups.append({'L': current['L'], 'R': current['R'], 'C': current['C']})
            current = {}
    return groups


def main():
    df = load_locations(EXCEL_PATH, EXCEL_SHEET)
    ser = select_serial_port()

    location_buffer = []

    root = tk.Tk()
    root.title('Component Fetcher')

    search_var = tk.StringVar()
    search_frame = tk.Frame(root)
    search_entry = tk.Entry(search_frame, textvariable=search_var, width=20)
    search_entry.pack(side=tk.LEFT, padx=5, pady=5)
    # Trigger search when the user presses Enter inside the entry field.
    search_entry.bind("<Return>", lambda event: handle_search())

    def handle_search():
        text = search_var.get()
        groups = parse_locations(text)
        for loc in groups:
            row = df[(df.L == loc['L']) & (df.R == loc['R']) & (df.C == loc['C'])]
            if not row.empty:
                x_cm = row.X_CM.iloc[0]
                y_cm = row.Y_CM.iloc[0]
                label = f"L{loc['L']:02d}-R{loc['R']:02d}-C{loc['C']:02d}"
                entry = (label, x_cm, y_cm)
                if entry not in location_buffer:
                    location_buffer.append(entry)
        list_var.set([e[0] for e in location_buffer])
        search_var.set('')

    search_btn = tk.Button(search_frame, text='Search', command=handle_search)
    search_btn.pack(side=tk.LEFT, padx=5)
    search_frame.pack()

    list_var = tk.StringVar(value=[])
    listbox = tk.Listbox(root, listvariable=list_var, width=30, height=10)
    listbox.pack(padx=5, pady=5)

    def handle_next():
        if location_buffer:
            label, x_cm, y_cm = location_buffer.pop(0)
            msg = f"X:{x_cm:.2f},Y:{y_cm:.2f}\n"
            ser.write(msg.encode('utf-8'))
            print(f"Sending to ESP32: {msg.strip()}")
            list_var.set([e[0] for e in location_buffer])


    def handle_exit():
        location_buffer.clear()
        list_var.set([])
        root.quit()

    root.protocol('WM_DELETE_WINDOW', handle_exit)

    next_btn = tk.Button(root, text='Next', command=handle_next)
    next_btn.pack(side=tk.LEFT, padx=5, pady=5)
    exit_btn = tk.Button(root, text='Exit', command=handle_exit)
    exit_btn.pack(side=tk.LEFT, padx=5, pady=5)

    def periodic():
        try:
            line = ser.readline().decode('utf-8')
        except Exception:
            line = ''
        if line == 'NEXT\n':
            handle_next()
        root.after(100, periodic)

    root.after(100, periodic)
    root.mainloop()
    ser.close()


if __name__ == '__main__':
    main()