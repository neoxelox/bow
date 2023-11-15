# https://thepoorengineer.com/en/arduino-python-plot/
import collections
import re
import time
from threading import Thread
from typing import List

import serial
from matplotlib.axes import Axes
from matplotlib.lines import Line2D


class Plotter:
    def __init__(
        self, port: str, baud: int, length: int, match: str, axes: Axes
    ) -> None:
        self.connection = serial.Serial(port, baud, timeout=5)
        self.length = length
        self.matcher = re.compile(match)
        self.axes = axes
        self.lines = []
        self.rawData = []
        self.data = []
        self.isRunning = True
        self.thread = None

    def run(self) -> None:
        if self.thread == None:
            self.thread = Thread(target=self.read)
            self.thread.start()

            # Block till we start receiving values
            while not self.rawData:
                time.sleep(0.1)

            # Create lines
            for i in range(len(self.rawData)):
                self.lines.append(self.axes.plot([], [], label=f"Num {i}")[0])
                self.data.append(
                    collections.deque([0.0] * self.length, maxlen=self.length)
                )

    def animate(self, frame: int) -> List[Line2D]:
        for i, raw in enumerate(self.rawData):
            self.data[i].append(float(raw))
            self.lines[i].set_data(range(self.length), self.data[i])

        return self.lines

    def read(self) -> None:
        # Give some buffer time for retrieving data
        time.sleep(1)

        self.connection.reset_input_buffer()

        while self.isRunning:
            values = self.matcher.findall(str(self.connection.readline()))
            if values:
                self.rawData = values

    def close(self) -> None:
        self.isRunning = False
        self.thread.join()
        self.connection.close()
