#
# Copyright (c) 2014 Jan de Visser (jan@sweattrails.com)
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc., 51
# Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
#
# This module uses ideas and code from:
# Copyright (c) 2012 Gustav Tiger (gustav@tiger.name)
#

import base64
import sys
import time
import threading

import gripe
import sweattrails.device.ant.fs.manager

logger = gripe.get_logger(__name__)


class GarminBridge(object):
    _lock = threading.RLock()
    
    def __init__(self, **kwargs):
        with GarminBridge._lock:
            if hasattr(GarminBridge, "_singleton") and GarminBridge._singleton:
                logger.error("GarminBridge is a singleton")
                sys.exit(1)
            self._condition = threading.Condition(GarminBridge._lock)
            logger.debug("__init__")
            self.bridge = None
            kwargs["callback"] = self
            self._garmin = kwargs.get("manager", 
                                      sweattrails.device.ant.fs.manager.Application(**kwargs))
            logger.debug("Created/assigned Garmin manager")
            self.init_config()
            GarminBridge._singleton = self
        
    def status_message(self, msg, *args):
        if self.bridge and hasattr(self.bridge, "status_message"):
            self.bridge.status_message(msg, *args)

    def init_config(self):
        if "garmin" not in gripe.Config:
            self.config = gripe.Config.set("garmin", {})
        else:
            self.config = gripe.Config.garmin
            
    def get_ant_product_ids(self, garmin):
        return ("finiandarcy.com", "Gripe", 0xBEEF)
        
    def get_passkey(self, garmin, serial):
        self.serial = serial 
        s = str(serial)
        if s not in self.config or \
                "passkey" not in self.config[s]:
            return None
        else:
            return base64.b64decode(self.config[s]["passkey"])
        
    def set_passkey(self, garmin, serial, passkey):
        s = str(serial)
        if s not in self.config:
            self.config[s] = {}
        self.config[s]["passkey"] = base64.b64encode(passkey)
        self.config = gripe.Config.set("garmin", self.config)
    
    def exists(self, antfile):
        return self.bridge.exists(antfile) \
            if self.bridge and hasattr(self.bridge, "exists") \
            else False
        
    def select(self, antfiles):
        return self.bridge.select(antfiles) \
            if self.bridge and hasattr(self.bridge, "select") \
            else []
        
    def process(self, antfile, data):
        if self.bridge and hasattr(self.bridge, "process"):
            self.bridge.process(antfile, data)
        
    def progress_init(self, msg, *args):
        if self.bridge and hasattr(self.bridge, "progress_init"):
            self.bridge.progress_init(msg, *args)

    def progress(self, new_progress):
        if self.bridge and hasattr(self.bridge, "progress"):
            self.bridge.progress(new_progress)
        
    def progress_end(self):
        if self.bridge and hasattr(self.bridge, "progress_end"):
            self.bridge.progress_end()
            
    def start(self):
        self._garmin.start()
        
    def on_start(self, garmin):
        logger.debug("on_start")
        
    def started(self, garmin):
        logger.debug("started")
        
    def connect(self):
        logger.debug("connect()")
        self._garmin.connect()
        
    def on_connect(self, garmin):
        logger.debug("on_connect")
        
    def connected(self, garmin):
        logger.debug("connected")
        
    def on_authentication(self, garmin):
        logger.debug("on_authentication")
        
    def authenticated(self, garmin):
        logger.debug("authenticated")
        
    def run(self):
        logger.debug("run()")
        self._garmin.run()
        
    def disconnect(self):
        logger.debug("disconnect()")
        self._garmin.disconnect()

    def on_disconnect(self, garmin):
        logger.debug("on_disconnect")
        
    def disconnected(self, garmin):
        logger.debug("disconnected")
        
    def stop(self):
        logger.debug("stop()")
        self._garmin.stop()
        
    def on_stop(self, garmin):
        logger.debug("on_stop")
        
    def stopped(self, garmin):
        logger.debug("stopped")
        
    def on_transport(self, garmin = None):
        if not hasattr(self, "_downloading"):
            try:
                self.progress_init("Downloading device directory")
                antfiles = self.download_files(garmin)
                self.progress_end()
        
                newfiles = filter(lambda f: not self.exists(f), antfiles)
                self._downloading = self.select(newfiles) or []
            except:
                logger.exception("Exception getting ANT device directory")
                raise
        l = len(self._downloading)
        self.status_message("Downloading {} file{}", l, "" if l == 1 else "s")

        # Download selected files:
        while self._downloading:
            f = self._downloading[0]
            self.progress_init("Downloading activity from {} ", f.get_date().strftime("%d %b %Y %H:%M"))
            data = self.download_file(f, garmin)
            self.progress_end()
            self.status_message("Processing activity from {} ", f.get_date().strftime("%d %b %Y %H:%M"))
            self.process(f, data)
            self.status_message("")
            del self._downloading[0]
        del self._downloading

    def download_file(self, f, garmin = None):
        garmin = garmin or self._garmin
        return garmin.download(f.get_index(), self)
        
    def download_files(self, garmin = None):
        garmin = garmin or self._garmin
        directory = garmin.download_directory(self)
        return directory.get_files()[2:]
        
    def downloads_pending(self):
        return hasattr(self, "_downloads") and self._downloads

    @classmethod
    def acquire(cls, appbridge):
        if not hasattr(cls, "_singleton") or not cls._singleton:
            cls._singleton = GarminBridge()
        gb = cls._singleton
        gb._condition.acquire()
        while gb.bridge:
            gb._condition.wait()
        gb.bridge = appbridge
        gb._condition.release()
        return gb
                
    def release(self):
        self._condition.acquire()
        self.bridge = None
        self._condition.notify()
        self._condition.release()

    def __enter__(self):
        logger.debug("Connecting Garmin")
        self.start()
        self.connect()
        return self
        
    def __exit__(self, exc_type, exc_value, exc_tb):
        logger.debug("Disconnecting Garmin")
        try:
            self.disconnect()
        finally:
            self.stop()
        self.release()
        return False

#
# ---------------------------------------------------------------------------
#  T E S T / S T A N D A L O N E  C O D E 
# ---------------------------------------------------------------------------
#

if __name__ == "__main__":
    import time
    import traceback

    class TestBridge(GarminBridge):
        def status_message(self, msg, *args):
            print(msg.format(*args))

        def progress_init(self, msg, *args):
            self.curr_progress = 0
            sys.stdout.write((msg + " [").format(*args))
            sys.stdout.flush()

        def progress(self, new_progress):
            diff = int(new_progress/10 - self.curr_progress) 
            sys.stderr.write("." * diff)
            sys.stdout.flush()
            self.curr_progress = new_progress/10

        def progress_end(self):
            sys.stdout.write("]\n")
            sys.stdout.flush()

        def exists(self, antfile):
            print("{0} / {1:02x} / {2}".format(
                antfile.get_date().strftime("%Y %b %d %H:%M"),
                antfile.get_type(), antfile.get_size()))
            return False

        def select(self, antfiles):
            return antfiles

        def process(self, antfile, data):
            print("Downloaded {0} / {1:02x} / {2}".format(
                antfile.get_date().strftime("%Y %b %d %H:%M"),
                antfile.get_type(), antfile.get_size()))

    def main():
        try:
            bridge = TestBridge(keep_alive = True)
            try:
                bridge.run()
            except (Exception, KeyboardInterrupt):
                traceback.print_exc()
                bridge.stop()
        except:
            traceback.print_exc()
            print("Interrupted")
            sys.exit(1)

    sys.exit(main())
