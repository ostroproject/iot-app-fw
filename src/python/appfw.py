"""IoT application framework interface

This module provides an interface to the IoT application framework. The
framework provides a simple inter application communication framework and
event system.

The main feature of this module is the IotApp class, which provides bindings
to the IoT application framework C-library. IotApp interfaces with a wrapper
library which implements the actual communication with the IoT application
framework library.

Special types used in documentation:
    U: type of user_data field of IotApp. Can be anything.
    W: type of data provided to the send_event callback.
    Json: anything compatible with Pythons json.loads() and json.dumps()
        methods and json-c library (eg. dict, list or string)
        NOTE: the data format may be restricted to Python dictionaries
            in the future.
"""

from __future__ import print_function  # for Python 3 compatibility
import _appfwwrapper as appfwwrapper
import pwd
import json
import inspect
import logging
import traceback


def _verify_signature(func, name, argc):
    if func == None or (callable(func) and
                        len(inspect.getargspec(func)[0]) == argc):
        return func
    else:
        raise TypeError("%s must be callable and accept exactly " +
                        "%d arguments" % (name, argc))

_logger = logging.getLogger("__appfw__")


class IotApp(object):
    """Bindings to the application framework wrapper library.

    NOTE:
        In case an exception is raised during the execution of any callback
        function, the IotApp aborts the execution of Python and prints
        the exception.
        If the user wants to terminate the program after receiving an event,
        the correct method is the quit the mainloop.

    Attributes:
        user_data (U): a reference to user defined data which is
        delivered to the status callback.
    """

    def __init__(self, event_handler=None, status_handler=None,
                 user_data=None):
        """(event_callback, status_callback, U) -> IotApp
        IotApp constructor

        The constructor is responsible of establishing a connection to the
        application framework and initializing the wrapper library.

        Args:
            event_handler (event_handler_callback): callback
                function which is called when an event is received.
            status_handler (status_handler_callback): callback
                function called when the event subscriptions are updated.
            user_data (U): initial user defined data which is provided to
                status callback invocations.
        """
        _logger.debug("__init__ IotApp")
        self.event_handler = event_handler
        self.status_handler = status_handler
        self.user_data = user_data
        appfwwrapper.init(self, self._event_handler,
                          self._status_handler, self._send_handler,
                          self._list_handler)
        # has to be called after initialization!
        self.subscriptions = set()
        # dictionaries and counter for 'send' and 'list' callbacks
        self._callbacks = {}
        self._arguments = {}
        self._callback_id = 0

    def __del__(self):
        _logger.debug("__del__ IotApp")
        appfwwrapper.clear()

    def enable_signals(self):
        """(None) -> None
        Request the delivery of certain signals as IoT events.

        Request the delivery of SIGHUP and SIGTERM signals as IoT events.
        The events are delivered to the 'event_handler'.
        """
        appfwwrapper.enable_signals()

    def send_event(self, event, data, callback=None,
                   callback_data=None, **target):
        """(str, json, send_callback, W) -> None
        Send a new event to the application framework.

        args:
            event (str): the name of event.
            data (Json): data attached to the event.
            callback (func(id, status, msg, callback_data) -> None): A
                callback function which is invoked after the emitting the
                events has finished. Signature specification below.
            callback_data (W): Data supplied to the callback function.
            **target: Keywords used to specify the target application(s).
                Recognized keywords are:
                    - label (str): SMACK label
                    - appid (str): application id
                    - binary (str): executed binary
                    - user (str): the name of the (linux) user of the target
                    - process (int): the process id.
                    NOTE: default values are interpreted as
                        broadcasting.
                    NOTE: at least one keyword argument must be
                        specified.

        Send callback specification:
            func(id, status, msg, user_data) -> None
            id (int): Internal application framework message ID
            status (int): ???
            msg (str): ???
            callback_data (W): Data provided to the 'send_event' function as
                callback_data
        """
        string_data = json.dumps(data)
        _logger.debug(str(string_data))
        # Remove Nones from target
        target = dict(filter(lambda item: item[1] is not None,
                             target.items()))

        if (callback != None):
            _verify_signature(callback, "Send callback", 4)
            self._callbacks[self._callback_id] = callback
            self._arguments[self._callback_id] = callback_data

        if ('user' in target and target['user']):
            target['user'] = pwd.getpwnam(target['user']).pw_uid

        appfwwrapper.send_event(event=event, json_string=string_data,
                                send_id=self._callback_id, **target)
        self._callback_id += 1

    def update_subscriptions(self):
        """(None) -> None
        Send the current set of subscriptions to the application
        framework.

        NOTE:
            This method must be called manually if the subscriptions are
            modified in place.
        """
        appfwwrapper.subscribe_events(list(self.subscriptions))

    def __list(self, list_function, callback, callback_data=None):
        """(list_function, list_callback, W) -> None
        Helper function for list_running and list_all. Contains the common
        functionality in order to avoid code duplication.

        args:
            list_function (func(callback_id))
                actual framework function to be called
            callback (func(app_list, id, status, msg, callback_data) -> None):
                Callback function. See list_all or list_running for
                documentation
            callback_data (W): Data supplied to the callback function.
        """
        try:
            _logger.debug("appfw, __list")
            _verify_signature(callback, "List callback", 5)
            self._callbacks[self._callback_id] = callback
            self._arguments[self._callback_id] = callback_data

            list_function(self._callback_id)

            self._callback_id += 1
        except Exception as e:
            traceback.print_exc()
            print("Zero status was returned from iot_app_list_* C-API")
            print(e.message)
            sys.exit(1)

    def list_running(self, callback, callback_data=None):
        """Send a request for the list of running applications to the
        application framework. Callback argument count is verified.

        args:
            callback (func(app_list, id, status, msg, callback_data) -> None)
                A callback function which is invoked eventually. See
                specification below.
            callback_data (W): Data supplied to the callback function.

         List callback specification:
            func(app_list, id, status, msg, callback_data) -> None
            app_list: List of applications. List contains dictionaries with
                strings 'appid' and 'desktop' as keys and associated values
                which are either string or None
            id (int): Internal application framework message ID
            status (int): ???
            msg (str): ???
            callback_data (W): Data provided to the 'list_running' function as
                callback_data
        """
        _logger.debug("appfw, list_running")
        self.__list(appfwwrapper.list_running, callback, callback_data)

    def list_all(self, callback, callback_data=None):
        """Send a request for the list of running applications to the
        application framework. Callback argument count is verified.

        args:
            callback (func(app_list, id, status, msg, callback_data) -> None)
                A callback function which is invoked eventually. See
                specification below.
            callback_data (W): Data supplied to the callback function.

         List callback specification:
            func(app_list, id, status, msg, callback_data) -> None
            app_list: List of applications. List contains dictionaries with
                strings 'appid' and 'desktop' as keys and associated values
                which are either string or None
            id (int): Internal application framework message ID
            status (int): ???
            msg (str): ???
            callback_data (W): Data provided to the 'list_all' function as
                callback_data
        """
        _logger.debug("appfw, list_running")
        self.__list(appfwwrapper.list_all, callback, callback_data)

    def _enable_debug(self, debug=["*"]):
        logging.basicConfig()
        _logger.setLevel(logging.DEBUG)
        _logger.debug("enable_debug")
        _logger.debug(debug)
        appfwwrapper.enable_debug(debug)

    def _event_handler(self, event, data):
        _logger.debug("Python internal event callback")
        try:
            json_data = json.loads(data)
            if (self._external_event_handler != None):
                self._external_event_handler(event, json_data)
        except Exception as e:
            # If exceptions are not caught here the wrapper library destroys
            # them.
            traceback.print_exc()
            print("Event handler threw an exception after receiving a " +
                  "callback. Aborting:")
            print(e.message)
            sys.exit(1)

    def _status_handler(self, seqno, status, msg, data):
        _logger.debug("Python internal status callback")
        try:
            json_data = json.loads(data)
            if (self._external_status_handler != None):
                self._external_status_handler(seqno, status, msg,
                                              json_data, self.user_data)
        except Exception as e:
            traceback.print_exc()
            print("Status handler threw an exception after receiving a " +
                  "callback. Aborting:")
            print(e.message)
            sys.exit(1)

    def _send_handler(self, callback_id, id, status, msg):
        _logger.debug("Python internal send callback")
        try:
            if (callback_id in self._callbacks):
                self._callbacks[callback_id](
                    id, status, msg, self._arguments[callback_id])
                del self._callbacks[callback_id]
                del self._arguments[callback_id]
        except Exception as e:
            traceback.print_exc()
            print("Send handler threw an exception after receiving a " +
                  "callback. Aborting:")
            print(e.message)
            sys.exit(1)

    def _list_handler(self, callback_id, id, status, msg, apps):
        _logger.debug("Python internal list callback")
        try:
            if (callback_id in self._callbacks):
                self._callbacks[callback_id](
                    apps, id, status, msg, self._arguments[callback_id])
                del self._callbacks[callback_id]
                del self._arguments[callback_id]

        except Exception as e:
            traceback.print_exc()
            print("List handler threw an exception after receiving a " +
                  "callback. Aborting:")
            print(e.message)
            sys.exit(1)

    @property
    def event_handler(self):
        """func(event, data) -> None: a callback function which is invoked
            when an event is received.

        Event callback specification:
            event (str): The name of an event.
            data (Json): The data associated with the event.
        """
        return self._external_event_handler

    @event_handler.setter
    def event_handler(self, handler):
        self._external_event_handler = _verify_signature(handler,
                                                         "Event handler",
                                                         2)

    @property
    def status_handler(self):
        """func(seqno, status, msg, data, user_data) -> None: a callback
            function which is invoked after event subscriptions.

        Status callback specification:
            seqno (int): Application framework sequence number of associated
                request.
            status (int): Request status (0 ok, non-zero error).
            msg (str): Error message.
            data (Json): Optional request-specific status data.
            user_data (U): A reference to 'user_data' supplied to the IotApp
                instance
        """
        return self._external_status_handler

    @status_handler.setter
    def status_handler(self, handler):
        self._external_status_handler = _verify_signature(handler,
                                                          "Status handler",
                                                          5)

    @property
    def subscriptions(self):
        """iterable: the set of events this IotApp instance is subscribed
            to.

        There are two ways to modify the subscriptions of an IotApp.
            -1 By assigning manually a list of event names to the
                'subscriptions' field, the 'IotApp' automatically updates
                the subscriptions on the application framework server.
                A single string is also accepted as a valid assignment.
            -2 By modifying the 'subscriptions' in place, the application
                framework server only updates the subscriptions after
                update_subscriptions call.

        NOTE: If a new list of events is assigned, a call to the
            status_callback will occur.

        Examples:
            >>> app = appfw.IotApp()
            >>> app.subscriptions = ["cat_event", "dog_event"]

            >>> app = appfw.IotApp()
            >>> app.subscriptions = "fox_event"

            >>> app = appfw.IotApp()
            >>> app.subscriptions.add("frog_event")
            >>> app.subscriptions.add("seal_event")
            >>> app.subscriptions.remove("frog_event")
            >>> app.update_subscriptions()
        """
        return self.__subscriptions

    @subscriptions.setter
    def subscriptions(self, subscriptions):
        if (isinstance(subscriptions, str)):
            self.__subscriptions = set({subscriptions})
        else:
            self.__subscriptions = set(subscriptions)
        self.update_subscriptions()
