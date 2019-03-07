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


import collections
import errno
import importlib
import json
import logging
import os
import os.path
import sys
import threading
import traceback

# imp gripe.autoreload
import gripe.json_util


class ConfigMeta(type):
    def __getattribute__(cls, name):
        if name in ("backup", "restore") or name.startswith("_"):
            return super(ConfigMeta, cls).__getattribute__(name)
        if not super(ConfigMeta, cls).__getattribute__("_loaded"):
            super(ConfigMeta, cls).__getattribute__("_load")()
        return super(ConfigMeta, cls).__getattribute__(name)

    def __setattr__(cls, name, value):
        if not name.startswith("_"):
            cls._sections.add(name)
        return super(ConfigMeta, cls).__setattr__(name, gripe.json_util.JSON.create(value))

    def __delattr__(cls, name):
        cls._sections.remove(name)
        return super(ConfigMeta, cls).__delattr__(name)

    def __len__(cls):
        return len(cls._sections)

    def __getitem__(cls, key):
        return getattr(cls, key)

    def __setitem__(cls, key, value):
        return setattr(cls, key, value)

    def __delitem__(cls, key):
        return delattr(cls, key)

    def __iter__(cls):
        return iter(cls._sections)

    def __contains__(cls, key):
        return key in cls._sections

    def keys(cls):
        return cls._sections


#############################################################################
#
#  E X C E P T I O N S
#
#############################################################################


class Error(Exception):
    """
        Base class for exceptions in this module.
    """
    pass


class NotSerializableError(Error):
    """
        Marker exception raised when when a non-JSON serializable property is
        serialized
    """
    def __init__(self, propname):
        self.propname = propname

    def __str__(self):
        return "Property %s is not serializable" % (self.propname,)


class AuthException(Error):
    pass

#############################################################################
#
##############################################################################


def _bootlog(msg):
    print(msg, file=sys.stderr)


_root_dir = None
_app_dirs = collections.OrderedDict()
_users_init = set([])


def root_dir():
    global _root_dir
    if not _root_dir:
        _root_dir = sys.modules["gripe"].__file__
        while _root_dir and not os.path.isdir(os.path.join(_root_dir, "conf")):
            _root_dir = os.path.dirname(_root_dir) if _root_dir != os.path.dirname(_root_dir) else None
        assert _root_dir, "No configuration directory found under %s" % sys.modules["gripe"].__file__
        _bootlog("_root_dir = %s" % _root_dir)
        # autoreload.trackdir(os.path.join(_root_dir, "conf"))
    return _root_dir


def user_dir(uid):
    if not _users_init:
        mkdir("users")
    userdir = os.path.join("users", uid)
    if not os.path.exists(userdir):
        mkdir(userdir)
    _users_init.add(uid)
    return userdir


def add_app_dir(app_name, app_dir):
    _bootlog("Adding application %s: %s" % (app_name, app_dir))
    d = os.path.abspath(app_dir)
    if os.path.exists(d):
        # _bootlog("Finding conf. directory starting from %s" % d)
        while d and d != os.path.dirname(d) and not os.path.isdir(os.path.join(d, "conf")):
            d = os.path.dirname(d)
        if d:
            # _bootlog("Using app.dir %s" % d)
            _app_dirs[app_name] = d
            # autoreload.trackdir(os.path.join(d, "conf"))
            sys.path.append(d)
        else:
            _bootlog("Could not find conf directory for app.dir %s" % app_dir)
    else:
        _bootlog("Abs.path %s for app.dir %s does not exist" % (app_dir, d))


def add_app(app):
    return add_app_dir(app, os.path.join(os.path.dirname(root_dir()), app))


def get_app_dir(app_name):
    return _app_dirs.get(app_name)


def get_app_dirs():
    return _app_dirs.values()


def read_file(fname):
    try:
        filename = os.path.join(root_dir(), fname)
        fp = open(filename, "r")
    except IOError:
        # print "IOError reading config file %s: %s" % (filename, e.strerror)
        return None
    else:
        with fp:
            return fp.read()


def write_file(fname, data, mode="w+"):
    filename = os.path.join(root_dir(), fname)
    with open(filename, mode) as fp:
        return fp.write(data)


def exists(f):
    p = os.path.join(root_dir(), f)
    return os.access(p, os.F_OK)


def unlink(f):
    p = os.path.join(root_dir(), f)
    try:
        if os.access(p, os.F_OK):
            if os.access(p, os.W_OK):
                os.unlink(p)
    except OSError as e:
        if e.errno != errno.ENOENT:
            raise


def rename(oldname, newname):
    o = os.path.join(root_dir(), oldname)
    n = os.path.join(root_dir(), newname)
    try:
        if os.access(o, os.F_OK):
            if os.access(o, os.W_OK):
                if not os.access(n, os.F_OK):
                    os.rename(o, n)
    except OSError as e:
        if e.errno != errno.ENOENT:
            raise


def listdir(dirname):
    return os.listdir(os.path.join(root_dir(), dirname))


def mkdir(dirname):
    try:
        os.mkdir(os.path.join(root_dir(), dirname))
        return True
    except IOError:
        return False


def resolve(funcname, default=None):
    if funcname:
        if callable(funcname):
            return funcname
        (modname, dot, fnc) = str(funcname).rpartition(".")
        logger().debug("Resolving function %s in module %s", fnc, modname)
        mod = importlib.import_module(modname)
        return getattr(mod, fnc) if hasattr(mod, fnc) and callable(getattr(mod, fnc)) else default
    else:
        return resolve(default, None) if not callable(default) else default


def hascallable(obj, attr):
    return hasattr(obj, attr) and callable(getattr(obj, attr))


def call_if_exists(obj, mth, fallback, *args, **kwargs):
    if hascallable(obj, mth):
        return getattr(obj, mth)(*args, **kwargs)
    else:
        return fallback(*args, **kwargs) if callable(fallback) else fallback


def get_logger(logger_name):
    return LogConfig.get(logger_name).get_logger()


def print_stack(logger_obj, caption):
    stack = traceback.format_stack()
    s = caption + "\n"
    for frame in stack[:-2]:
        s += frame
    logger_obj.debug(s)


_logger = None


def logger():
    global _logger
    if _logger is None:
        _logger = get_logger(__name__)
    return _logger


class abstract:
    def __init__(self, *args):
        self._methods = args

    def __call__(self, cls):
        for method in self._methods:
            if isinstance(method, tuple):
                m = method[0]
                doc = method[1]
                decorator = getattr(__builtins__, method[2]) if len(method) > 2 else None
            else:
                m = str(method)
                doc = None
                decorator = None

            def wrapper(instance):
                n = instance.__name__ if isinstance(instance, type) else instance.__class__.__name__
                assert 0, "Method %s of class %s is abstract" % (method, n)

            wrapper.__doc__ = doc
            if decorator:
                wrapper = decorator(wrapper)
            setattr(cls, m, wrapper)
        return cls


class LoopDetector(set):
    _tl = threading.local()

    def __init__(self):
        super(LoopDetector, self).__init__()
        self.count = 0
        self.loop = False
        LoopDetector._tl.detector = self

    def __enter__(self):
        self.count += 1
        return self

    def __exit__(self, *args):
        self.count -= 1
        self.loop = False
        if not self.count:
            del LoopDetector._tl.detector

    @classmethod
    def begin(cls, obj=None):
        ret = LoopDetector._tl.detector if hasattr(LoopDetector._tl, "detector") else LoopDetector()
        if obj is not None:
            if obj in ret:
                ret.loop = True
            else:
                ret.add(obj)
        return ret


class LoggerSwitcher(object):
    def __init__(self, packages, l):
        self._backup = {}
        self._packages = packages if isinstance(packages, (list, tuple)) else [packages]
        self._logger = l

    def __enter__(self):
        for p in self._packages:
            if hasattr(p, "logger"):
                _bootlog("Switching logger for package %s" % p.__name__)
                self._backup[p] = p.logger
                p.logger = self._logger
        return self

    def __exit__(self, *args):
        for p in self._backup:
            p.logger = self._backup[p]

    @classmethod
    def begin(cls, packages, l):
        return LoggerSwitcher(packages, l)


class ContentType(object):
    Binary, Text = range(2)
    _by_ext = {}
    _by_content_type = {}

    def __init__(self, ext, ct, typ):
        self.content_type = ct
        self.extension = ext
        self.type = typ
        self.__class__._by_ext[ext] = self
        self.__class__._by_content_type[ct] = self

    def __str__(self):
        return "%s (%s, %s)" % (self.content_type, self.extension, self.type)

    def is_text(self):
        return self.type == self.__class__.Text

    def is_binary(self):
        return self.type == self.__class__.Binary

    @classmethod
    def for_extension(cls, ext, default=None):
        return cls._by_ext.get(ext, default)

    @classmethod
    def for_path(cls, path, default=None):
        (_, ext) = os.path.splitext(path)
        return cls.for_extension(ext, default)

    @classmethod
    def for_content_type(cls, ct, default=None):
        return cls._by_content_type.get(ct, default)


JSON = ContentType(".json", "application/json", ContentType.Text)
JPG = ContentType(".jpg", "image/jpeg", ContentType.Binary)
GIF = ContentType(".gif", "image/gif", ContentType.Binary)
PNG = ContentType(".png", "image/png", ContentType.Binary)
JS = ContentType(".js", "text/javascript", ContentType.Text)
CSS = ContentType(".css", "text/css", ContentType.Text)
XML = ContentType(".xml", "text/xml", ContentType.Text)
TXT = ContentType(".txt", "text/plain", ContentType.Text)
HTML = ContentType(".html", "text/html", ContentType.Text)


class Config(metaclass=ConfigMeta):
    _loaded = False
    _sections = set([])

    # We set a bunch of dummies here to keep IDEs happy:
    logging = {}
    app = {}
    database = {}
    gripe = {}
    grit = {}
    grumble = {}
    model = {}
    qtapp = {}
    smtp = {}
    sweattrails = {}

    @classmethod
    def get_key(cls, key):
        keypath = key.split(".")
        obj = cls
        for p in keypath:
            if hasattr(obj, p):
                obj = getattr(obj, p)
            else:
                return None
        return obj

    @classmethod
    def resolve(cls, path, default=None):
        value = cls.get_key(path)
        return resolve(value, default)

    @classmethod
    def as_dict(cls):
        return {section: getattr(cls, section) for section in cls._sections}

    @classmethod
    def as_json(cls):
        return json.dumps(cls.as_dict())

    @classmethod
    def key(cls, path):
        p = path.strip()
        p = p.split('.') if p else []
        d = cls
        ix = 0
        while ix < len(p):
            if p[ix] in d:
                if isinstance(d[p[ix]], dict):
                    d = d[p[ix]]
                    ix += 1
                else:
                    return d[p[ix]] if ix == len(p) - 1 else {}
            else:
                return {}
        return d

    @classmethod
    def set(cls, section, config):
        config = gripe.json_util.JSONObject(config) \
            if not isinstance(config, gripe.json_util.JSONObject) \
            else config
        if (not exists(os.path.join("conf", "%s.json.backup" % section)) and
                exists(os.path.join("conf", "%s.json" % section))):
            rename(os.path.join("conf", "%s.json" % section),
                   os.path.join("conf", "%s.json.backup" % section))
        config.file_write(os.path.join("conf", "%s.json" % section), 4)
        setattr(cls, section, config)
        return config

    @classmethod
    def _load_file(cls, conf_dir, section, conf_file):
        _bootlog("Reading conf file %s/%s into section %s" % (conf_dir, conf_file, section))
        config = gripe.json_util.JSON.file_read(os.path.join(conf_dir, conf_file))
        if config:
            if ("include" in config) and isinstance(config.include, list):
                for include in config.include:
                    _bootlog("Reading include conf file %s.inc into section %s" % (include, section))
                    inc = cls._load_file(conf_dir, section, "%s.inc" % include)
                    if inc:
                        config.merge(inc)
                del config["include"]
            if ("applications" in config) and isinstance(config.applications, list):
                for app in config.applications:
                    add_app(app)
                del config.applications
        return config

    @classmethod
    def _load_dir(cls, dir_name):
        d = dir_name if dir_name.endswith("conf") else os.path.join(dir_name, "conf")
        _bootlog("Reading configuration from %s" % d)
        for f in filter(lambda fname: fname.endswith(".json"), os.listdir(d)):
            (section, _) = os.path.splitext(f)
            c = cls._load_file(d, section, f)
            config = getattr(cls, section) if hasattr(cls, section) else None
            if config and c:
                config.merge(c)
            setattr(cls, section, config if config else c)

    @classmethod
    def _load(cls):
        cls._loaded = True
        # logging is special. We always want it.
        setattr(cls, "logging", gripe.json_util.JSONObject())
        cls._load_dir(root_dir())
        for d in _app_dirs.values():
            cls._load_dir(d)

        # Tie our logging config into the platform's by pre-initializing all loggers
        # we know of. This way we can use propagate = True to combine logging across
        # a package.
        #
        # FIXME: I should really do this properly and have the platform logging use
        # gripe.Config.
        for name in filter(lambda n: n in ("__root__", "__main__") or not n.startswith("_"), cls["logging"].keys()):
            LogConfig.get(name if name != "__root__" else "").get_logger()

    @classmethod
    def backup(cls):
        for s in cls._sections:
            config = getattr(cls, s)
            unlink(os.path.join("conf", "%s.json.backup" % s))
            if config is not None:
                config.file_write(os.path.join("conf", "%s.json.backup" % s), 4)

    @classmethod
    def restore(cls):
        for f in os.listdir(os.path.join(root_dir(), "conf")):
            (section, ext) = os.path.splitext(f)
            if ext == ".json":
                unlink(os.path.join("conf", f))
        for f in os.listdir(os.path.join(root_dir(), "conf")):
            (section, ext) = os.path.splitext(f)
            if ext == ".backup":
                os.rename(os.path.join(root_dir(), "conf", "%s.json.backup" % section),
                          os.path.join(root_dir(), "conf", "%s.json" % section))


# Configure logging


class LoggerProxy(object):
    def __init__(self, logger_obj):
        self._logger = logger_obj

    def __getattr__(self, attr_name):
        return getattr(self._logger, attr_name)


class LogConfig(object):
    _configs = {}
    _defaults = None
    _default_config = {
        "level": logging.INFO,
        "destination": "stderr",
        "filename": None,
        "append": False,
        "format": "%(name)-15s:%(asctime)s:%(levelname)-7s:%(message)s",
        "dateformat": "%y%m%d %H:%M:%S"
    }

    def __init__(self, name=None, config=None):
        if config:
            self._build(name, config)
        else:
            if LogConfig._defaults is None:
                LogConfig._defaults = LogConfig("builtin.defaults",
                                                LogConfig._default_config)
            self.name = name if name else ""
            self.config = self._get_config()
            self.parent = self._get_parent()
            self._logger = None
            self._handler = None

            self.log_level = getattr(logging, self.config["level"].upper()) \
                if "level" in self.config \
                else self.parent.log_level

            self.destination = self.config.get(
                                   "destination",
                                   self.parent.destination).lower()
            assert self.destination in ("stderr", "file"),\
                "Invalid logging destination %s" % self.destination
            self.filename = self.config.get("filename")
            self.append = self.config.get("append", self.parent.append)
            self.flat = self.config.get("flat", not bool(self.name))
            self.format = self.config.get("format", self.parent.format)
            self.dateformat = self.config.get("dateformat", self.parent.dateformat)

    def _build(self, name, config):
        self.name = name
        self.config = config
        self.parent = None
        self._logger = None
        self._handler = None

        self.log_level = config.get("level")
        self.destination = config.get("destination")
        self.filename = config.get("filename")
        self.append = config.get("append")
        self.format = self.config.get("format")
        self.dateformat = self.config.get("dateformat")
        self.flat = True

    def _get_config(self):
        ret = Config["logging"].get(self.name) \
            if self.name \
            else Config["logging"].get("__root__")
        return ret or {}

    def _get_parent(self):
        if not self.name:
            return LogConfig._defaults
        else:
            (parent, _, _) = self.name.rpartition(".")
            return LogConfig.get(parent)

    def _get_filename(self):
        if self.filename or not self.parent:
            return self.filename
        else:
            return self.parent._get_filename()

    def get_filename(self):
        ret = self._get_filename()
        if ret is None:
            ret = (self.name if self.name else "__grumble__") + ".log"
        return ret

    def _create_file_handler(self):
        mkdir("logs")
        mode = "a" if self.append else "w"
        return logging.FileHandler(os.path.join(root_dir(), "logs", self.get_filename()), mode)

    def _create_stderr_handler(self):
        return logging.StreamHandler(sys.stderr)

    def _get_handler(self):
        if not self._handler:
            formatter = logging.Formatter(self.format, self.dateformat)
            handler_factory = self._get_handler_factory(self)
            self._handler = handler_factory()
            self._handler.setFormatter(formatter)
        return self._handler

    def get_logger(self):
        if not self._logger:
            self._logger = logging.getLogger(self.name)
            self._logger.propagate = not self.flat
            self._logger.setLevel(self.log_level)
            if self.flat:
                self._logger.addHandler(self._get_handler())
        return self._logger

    @classmethod
    def _get_handler_factory(cls, config):
        return getattr(config, "_create_%s_handler" % config.destination)

    @classmethod
    def get(cls, logger_name):
        logger_name = logger_name or ""
        ret = LogConfig._configs.get(logger_name)
        if not ret:
            ret = LogConfig(logger_name)
            LogConfig._configs[logger_name] = ret
        return ret


logging.basicConfig(stream=sys.stderr,
                    level=LogConfig._default_config["level"],
                    datefmt=LogConfig._default_config["dateformat"],
                    format=LogConfig._default_config["format"])

if __name__ == "__main__":
    Config.backup()
