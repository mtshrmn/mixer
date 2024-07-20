import hid
import time
from typing import List

VENDOR_ID = 0x2341
PRODUCT_ID = 0x8037

START_OF_TEXT = 2
END_OF_TEXT = 3
START_OF_HEADING = 1


class Device:
    def __init__(self):
        self.device =  hid.device()
        self.connected = False
        self.attempt_open()

    def attempt_open(self) -> bool:
        if self.connected:
            return True
        
        try:
            self.device.open(VENDOR_ID, PRODUCT_ID)
            print("[DEBUG] device opened")
            self.connected = True
            return True
        except OSError:
            self.connected = False
            return False

    
    def write(self, data: List[str]) -> None:
        encoded: List[int] = [START_OF_TEXT]
        for data_item in data:
            encoded.append(START_OF_HEADING)
            encoded.extend([ord(c) for c in data_item])
        encoded.append(END_OF_TEXT)
        print(f"[DEBUG] writing: {encoded}")
        self.__write(encoded)

    def read(self, size: int) -> List[int]:
        while not self.attempt_open():
            # TODO: reimplement it in a non-busy wait
            time.sleep(1)
        return self.device.read(size)


    def __write(self, data: List[int]) -> None:
        if self.attempt_open():
            self.device.write(data)

