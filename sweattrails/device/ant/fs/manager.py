# Ant-FS
#
# Copyright (c) 2012, Gustav Tiger <gustav@tiger.name>
#               2014, Jan de Visser <jan@ssweattrails.com>
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

import array
import struct
import threading
import queue

import sweattrails.device.ant.fs.command

from sweattrails.device.ant.easy.channel import Channel
from sweattrails.device.ant.easy.node import Message
from sweattrails.device.ant.easy.node import Node
from sweattrails.device.ant.fs.beacon import Beacon
from sweattrails.device.ant.fs.command import AuthenticateCommand
from sweattrails.device.ant.fs.command import AuthenticateResponse
from sweattrails.device.ant.fs.command import DisconnectCommand
from sweattrails.device.ant.fs.command import DownloadRequest
from sweattrails.device.ant.fs.command import DownloadResponse
from sweattrails.device.ant.fs.command import LinkCommand
from sweattrails.device.ant.fs.command import PingCommand
from sweattrails.device.ant.fs.file import Directory

import gripe

_logger = gripe.get_logger(__name__)
debug_protocol = False


class AntFSException(Exception):
    pass


class AntFSDownloadException(AntFSException):
    def __init__(self, error):
        self.error = error


class AntFSAuthenticationException(AntFSException):
    def __init__(self, error):
        self.error = error


class AntConnectionException(AntFSException):
    def __str__(self):
        return "Could not connect to device"


class Manager(object):
    _vendor_id  = 0x0fcf
    _vendor_name = "ant.py"
    _product_id = 0x1009
    _product_name = "ant-fs manager"
    _serial_number = 1337
    
    _frequency     = 19    # 0 to 124, x - 2400 (in MHz)
    
    def __init__(self, **kwargs):
        _logger.debug("Initializing ANT-FS Manager")
        self._statuslogger = kwargs.get("statuslogger")
        self._callback = kwargs.get("callback")
        if (not self._statuslogger and 
                self._callback and
                hasattr(self._callback, "status_message")):
            self._statuslogger = self._callback
        self._status_msg("Initializing ANT-FS Manager")
            
        if self._callback and hasattr(self._callback, "get_ant_product_ids"):
            ids = self._callback.get_ant_product_ids(self)
            if len(ids) == 5:
                (self._vendor_id, self._product_id,
                 self._vendor_name, self._product_name,
                 self._serial_number) = ids
            elif len(ids) == 3:
                (self._vendor_name, self._product_name,
                 self._serial_number) = ids
            
        _logger.debug("Vendor: %x (%s)", self._vendor_id, self._vendor_name)
        _logger.debug("Product: %x (%s)", self._product_id, self._product_name)
        _logger.debug("Serial#: %x", self._serial_number)
        
        self.keep_alive = kwargs.get("keep_alive", True)
        _logger.debug("Keep-alive: %s", self.keep_alive)
        
        self._timer = None
        if self.keep_alive:
            self._timer_lock = threading.RLock()

        self._lock = threading.RLock()
        self._queue = queue.Queue()
        self._beacons = queue.Queue()

        
    #===========================================================================
    # P R I V A T E  M E T H O D S
    #===========================================================================
    
    def _status_msg(self, msg, *args):
        if self._statuslogger:
            self._statuslogger.status_message(msg, *args)
        
    def _on_beacon(self, data):
        b = Beacon.parse(data)
        self._beacons.put(b)

    def _on_command(self, data):
        c = sweattrails.device.ant.fs.command.parse(data)
        self._queue.put(c)

    def _on_data(self, data):
        #print "_on_data", data, len(data)
        if data[0] == 0x43:
            self._on_beacon(data[:8])
            if len(data[8:]) > 0:
                self._on_command(data[8:])
        elif data[0] == 0x44:
            self._on_command(data)
    
    def _get_beacon(self):
        b = self._beacons.get()
        self._beacons.task_done()
        return b
    
    def _get_command(self, timeout=3.0):
        _logger.debug("Get command, t%d, s%d", timeout, self._queue.qsize())
        c = self._queue.get(True, timeout)
        self._queue.task_done()
        return c
    
    def _send_command(self, c, ignore_timeout = False):
        _logger.debug("Sending '%s' command", c.__class__.__name__)
        data = c.get()
        if len(data) <= 8:
            self._channel.send_acknowledged_data(data, ignore_timeout)
        else:
            self._channel.send_burst_transfer(data)
            
    def _setup_channel(self):
        # FIXME: Make channel params configurable by callback
        self._channel.set_period(4096)
        self._channel.set_search_timeout(255)
        self._channel.set_rf_freq(50)
        self._channel.set_search_waveform([0x53, 0x00])
        self._channel.set_id(0, 0x01, 0)
        
        self._channel.open()
        #channel.request_message(Message.ID.RESPONSE_CHANNEL_STATUS)
        self._status_msg("Searching...")

    def _keep_alive(self, ping_now = True):
        if self.keep_alive:
            _logger.debug("Sending keep-alive Ping command")
            with self._timer_lock:
                self._timer = None
                if ping_now:
                    self.ping()
                self._timer = threading.Timer(1, self._keep_alive)
                self._timer.start()
        
    def _cancel_keep_alive(self):
        if self.keep_alive:
            _logger.debug("Canceling keep-alive timer")
            with self._timer_lock:
                if self._timer:
                    self._timer.cancel()
                    self._timer = None
    
    #===========================================================================
    # P U B L I C  M E T H O D S
    #===========================================================================
    
    def authenticate(self, callback = None):
        callback = callback or self._callback
        if hasattr(callback, "on_authentication"):
            callback.on_authentication(self)
        if hasattr(callback, "get_ant_serial_number"):
            self._serial_number = callback.get_ant_serial_number(self)
        self._peer_serial, self._peer_name = self.authentication_serial()
        passkey = callback.get_passkey(self, self._peer_serial)
        self._status_msg("Authenticating with {} ({})...", self._peer_name, self._peer_serial)
        _logger.debug("serial %s, %r, %r", self._peer_name, self._peer_serial, passkey)
        
        if passkey is not None:
            self._status_msg("Authenticating with {} ({})... Passkey... ", self._peer_name, self._peer_serial)
            try:
                self.authentication_passkey(passkey)
                self._status_msg("Authenticating with {} ({})... Passkey... OK.", self._peer_name, self._peer_serial)
            except sweattrails.device.ant.fs.manager.AntFSAuthenticationException:
                self._status_msg("Authenticating with {} ({})... Passkey... FAILED", self._peer_name, self._peer_serial)
                raise
        else:
            self._status_msg("Authenticating with {} ({})... Pairing... ", self._peer_name, self._peer_serial)
            try:
                passkey = self.authentication_pair(self._product_name)
                callback.set_passkey(self, self._peer_serial, passkey)
                self._status_msg("Authenticating with {} ({})... Pairing... OK.", self._peer_name, self._peer_serial)
            except sweattrails.device.ant.fs.manager.AntFSAuthenticationException:
                self._status_msg("Authenticating with {} ({})... Pairing... FAILED", self._peer_name, self._peer_serial)
                raise
        if hasattr(callback, "authenticated"):
            callback.authenticated(self)

    def connect(self, callback = None):
        callback = callback or self._callback
        if callback and hasattr(callback, "on_connect"):
            callback.on_connect(self)
        try:
            self._beacon = self._get_beacon()
            if self.link(callback):
                _logger.debug("connect: Link established")
                for _ in range(0, 5):
                    self._beacon = self._get_beacon()
                    if self._beacon.get_client_device_state() == Beacon.ClientDeviceState.AUTHENTICATION:
                        _logger.debug("connect: Beacon found")
                        self.authenticate(callback)
                        self._beacon = self._get_beacon()
                        self._keep_alive(False)
                        if callback and hasattr(callback, "connected"):
                            callback.connected(self)
                        return
            raise AntConnectionException()
        except:
            _logger.exception("ANT Manager connect")
            raise
        
    def start(self, callback = None):
        callback = callback or self._callback
        if callback and hasattr(callback, "on_start"):
            callback.on_start(self)
            
        self._node = Node(self._vendor_id, self._product_id)
        _logger.debug("Manager: Node initialized")

        self._status_msg("Request basic information...")
        m = self._node.request_message(Message.ID.RESPONSE_VERSION)
        self._status_msg("  ANT version:   {}", struct.unpack("<10sx", m[2])[0])
        m = self._node.request_message(Message.ID.RESPONSE_CAPABILITIES)
        self._status_msg("  Capabilities:  {}", m[2])
        m = self._node.request_message(Message.ID.RESPONSE_SERIAL_NUMBER)
        self._status_msg("  Serial number: {}", struct.unpack("<I", m[2])[0])
        _logger.debug("Application: retrieved basic info")

        self._status_msg("Starting system...")

        NETWORK_KEY= [0xa8, 0xa4, 0x23, 0xb9, 0xf5, 0x5e, 0x63, 0xc1]

        self._node.reset_system()
        self._node.set_network_key(0x00, NETWORK_KEY)

        self._channel = self._node.new_channel(Channel.Type.BIDIRECTIONAL_RECEIVE)
        self._channel.on_broadcast_data = self._on_data
        self._channel.on_burst_data = self._on_data
        self._setup_channel()
            
        self._worker_thread = threading.Thread(target=self._node.start, name="ant.fs")
        self._worker_thread.start()
        
        if callback and hasattr(callback, "started"):
            callback.started(self)

    def stop(self, callback = None):
        callback = callback or self._callback
        if callback and hasattr(callback, "on_stop"):
            callback.on_stop(self)
        if hasattr(self, "_node"):
            self._node.stop()
        if callback and hasattr(callback, "stopped"):
            callback.stopped(self)
    
    def erase(self, index):
        pass
    
    def upload(self, index, data):
        pass
    
    def download(self, index, callback = None):
        callback = callback or self._callback
        offset  = 0
        crc     = 0
        data    = array.array('B')
        with self._lock:
            while True:
                _logger.debug("Download %d, o%d, c%d", index, offset, crc)
                self._send_command(DownloadRequest(index, offset, True, crc))
                _logger.debug("Wait for response...")
                try:
                    response = self._get_command()
                    if response._get_argument("response") == DownloadResponse.Response.OK:
                        _logger.debug("Response OK")
                        remaining    = response._get_argument("remaining")
                        offset       = response._get_argument("offset")
                        total        = offset + remaining
                        size         = response._get_argument("size")
                        if debug_protocol:
                            _logger.debug("remaining %d offset %d total %d size %d", remaining, offset, total, response._get_argument("size"))
                        data[offset:total] = response._get_argument("data")[:remaining]
                        if callback != None:
                            progress = int((float(total) / float(response._get_argument("size"))) * 100)
                            if hasattr(callback, "progress"):
                                callback.progress(progress)
                            else:
                                callback(progress)
                        if total == size:
                            return data
                        crc = response._get_argument("crc")
                        if debug_protocol:
                            _logger.debug("crc: %d", crc)
                        offset = total
                    else:
                        raise AntFSDownloadException(response._get_argument("response"))
                except Queue.Empty:
                    _logger.debug("Download %d timeout", index)
                except AntFSException:
                    raise
                except:
                    _logger.exception("Exception in download. Attempting to recover...")
    
    def download_directory(self, callback = None):
        data = self.download(0, callback)
        return Directory.parse(data)
    
    def ping(self):
        """
            Pings the device. Used by the keep-alive thread.
        """
        with self._lock:
            self._send_command(PingCommand())

    def get_beacon(self):
        return self._beacon
    
    def get_channel(self):
        return self._channel

    def link(self, callback = None):
        callback = callback or self._callback
        if callback and hasattr(callback, "on_link"):
            callback.on_link(self)
        with self._lock:
            self._channel.request_message(Message.ID.RESPONSE_CHANNEL_ID)
            self._send_command(LinkCommand(self._frequency, 4, self._serial_number))
           
            # New period, search timeout
            self._channel.set_period(4096)
            self._channel.set_search_timeout(3)
            self._channel.set_rf_freq(self._frequency)
        if callback and hasattr(callback, "linked"):
            callback.on_linked(self)
        return True

    def authentication_serial(self):
        with self._lock:
            self._send_command(AuthenticateCommand(
                    AuthenticateCommand.Request.SERIAL,
                    self._serial_number))
            response = self._get_command()
            return (response.get_serial(), response.get_data_string())

    def authentication_passkey(self, passkey):
        with self._lock:
            self._send_command(AuthenticateCommand(
                    AuthenticateCommand.Request.PASSKEY_EXCHANGE,
                    self._serial_number, passkey))
    
            response = self._get_command()
            if response._get_argument("type") == AuthenticateResponse.Response.ACCEPT:
                return response.get_data_array()
            else:
                raise AntFSAuthenticationException(response._get_argument("type"))

    def authentication_pair(self, friendly_name):
        with self._lock:
            data = array.array('B', map(ord, list(friendly_name)))
            self._send_command(AuthenticateCommand(
                    AuthenticateCommand.Request.PAIRING,
                    self._serial_number, data))
    
            response = self._get_command(30)
            if response._get_argument("type") == AuthenticateResponse.Response.ACCEPT:
                return response.get_data_array()
            else:
                raise AntFSAuthenticationException(response._get_argument("type"))

    def disconnect(self, callback = None):
        callback = callback or self._callback
        if callback and hasattr(callback, "on_disconnect"):
            callback.on_disconnect(self)
        self._cancel_keep_alive()
        with self._lock:
            d = DisconnectCommand(DisconnectCommand.Type.RETURN_LINK, 0, 0)
            self._send_command(d, True)
        if callback and hasattr(callback, "disconnected"):
            callback.disconnected(self)


class Application(Manager):
    def __init__(self, **kwargs):
        super(Application, self).__init__(**kwargs)

    def run(self, callback = None):
        callback = callback or self._callback
        self.start(callback)
        try:
            self.connect(callback)
            try:
                callback.on_transport(self)
            finally:
                self.disconnect(callback)
        except:
            _logger.exception("Exception in ANT manager main loop")
            raise
        finally:
            self.stop(callback)
