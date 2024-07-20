import os
import json
import typing
import subprocess
from device import Device
from threading import Thread
from typing import NamedTuple, List, Dict, NoReturn, TypeAlias, Iterable

PactlSinkFull: TypeAlias = Dict[str, str | int | Dict[str, str | int]]

class PactlSink(NamedTuple):
    id: int
    name: str

class ProcessResult(NamedTuple):
    exitcode: int
    stdout: bytes
    stderr: bytes


class PactlError(Exception):
    pass


class Mixer:
    def __init__(self):
        if os.geteuid() != 0:
            raise PermissionError("Not running as root")
        id_process_result = self.__run(["id", "-u", "-n", "1000"])
        self.user = id_process_result.stdout
        self.device = Device()

        self.sinks = list(self.__list_sinks())
        self.subscription = Thread(target=self.__subscribe_to_pactl_events)
        self.subscription.start()
        self.__update_volume()

    def __update_volume(self) -> NoReturn:
        while True:
            data = self.device.read(len(self.sinks))
            print(f"[DEBUG] received: {data}")
            for sink, volume in zip(self.sinks, data):
                print(f"[DEBUG] {sink} {volume}")
                self.__set_sink_volume(sink, volume)

    def __set_sink_volume(self, sink: PactlSink, volume: int) -> None:
        volume = max(0, min(100, volume))
        cmd = ["set-sink-input-volume", f"{sink.id}", f"{volume}%"]
        self.__run_pactl(cmd, parse=False)

    def __subscribe_to_pactl_events(self) -> NoReturn:
        run_cmd_as = ["runuser", "-u", self.user, "--"]
        pactl = ["pactl", "--format=json", "subscribe"] 
        environment = {"XDG_RUNTIME_DIR": "/run/user/1000" }
        subscription = subprocess.Popen(run_cmd_as + pactl,
                                        text=True,
                                        env=environment,
                                        stdout=subprocess.PIPE)
        with subscription:
            if not subscription.stdout:
                raise PactlError("command `pactl subscribe` could not be run")

            for line in map(json.loads, subscription.stdout):
                if line["on"] != "sink-input":
                    continue
                if line["event"] in ("new", "remove"):
                    self.__update_sinks(line)
                    sink_names = list(map(lambda s: s.name, self.sinks))
                    self.device.write(sink_names)
            raise PactlError("command `pactl subscribe` stopped unexpectedly")

    def __update_sinks(self, sink: PactlSinkFull) -> None:
        # goal: keep `self.sinks` sorted by time of creation
        new = set(self.__list_sinks())
        old = set(self.sinks)
        if sink["event"] == "new":
            new_sinks = list(new - old)
            new_sinks.extend(self.sinks)
            self.sinks = new_sinks

        if sink["event"] == "remove":
            stale = old - new
            self.sinks = [sink for sink in self.sinks if sink not in stale]

    def __list_sinks(self) -> Iterable[PactlSink]:
        sinks_raw = self.__run_pactl(["list", "sink-inputs"])
        for sink in sinks_raw:
            props = typing.cast(Dict[str, str | int], sink["properties"])
            app = props["application.name"]
            media = props["media.name"]
            name = f"{app} {media}"
            id = typing.cast(int, sink["index"])
            yield PactlSink(id, name) 

    @staticmethod
    def __run(cmd: list[str], **kwargs) -> ProcessResult:
        r = subprocess.run(cmd, capture_output=True, **kwargs, check=False)
        stdout = r.stdout.rstrip()
        stderr = r.stderr.rstrip()
        return ProcessResult(r.returncode, stdout, stderr)

    def __run_pactl(self, cmd: list[str], parse=True) -> List[PactlSinkFull]:
        run_cmd_as = ["runuser", "-u", self.user, "--"]
        pactl = ["pactl", "--format=json"]
        environment = {"XDG_RUNTIME_DIR": "/run/user/1000" }
        result = self.__run(run_cmd_as + pactl + cmd, env=environment)
        if parse:
            return json.loads(result.stdout)
        return []

if __name__ == "__main__":
    m = Mixer()
