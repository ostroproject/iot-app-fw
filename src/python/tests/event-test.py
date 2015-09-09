import argparse
import glib
import appfw
import logging
import sys
logger = logging.getLogger("__event-test__")

#Create mainloop in default context
ml = glib.MainLoop(None)

def event_handler(event, data):
	logger.debug("Received an event with event = " + str(event) + \
	             ", data = " + str(data))
	if (event == 'system::terminate'):
		print "Received a SIGTERM, quitting mainloop..."
		ml.quit() #This is the proper way to quit
	elif (event == 'system::reload'):
		print "Received SIGHUP, doing nothing..."

def status_handler(seqno, status, msg, data, user_data):
	logger.debug("Received a status message with status = " + str(status) + \
	             ", msg = " + str(msg) + ", data = " + str(data) +
	             ", user_data " + str(user_data))
	if (status == 0):
		print "Succesfully set new event subscriptions."
		print "New subscriptions are: " + str(user_data.subscriptions)
	else:
		print "Event subscription failed. Is the appfw server running?"

def send_handler_burst(id, status, msg, callback_data):
	logger.debug("Sent a burst event with id = " + str(id) + \
	             ", status = " + str(status) + ", msg = " + str(msg) + \
	             ", callback_data = " + str(callback_data))

def send_handler_timed(id, status, msg, callback_data):
	logger.debug("Sent a timed event with id = " + str(id) + \
	             ", status = " + str(status) + ", msg = " + str(msg) + \
	             ", callback_data = " + str(callback_data))

def event_sender_factory(event, event_data, func, callback_data, **kwargs):
	def event_sender(app, count):
		if event_sender.counter >= count:
			return False
		else:
			app.send_event(event, event_data, func, callback_data, **kwargs)
			event_sender.counter += 1
			return True

	event_sender.counter=0
	return event_sender

def be_server(app, args):
	app.user_data = app #Allows listing of subscriptions from status handler
	#Set subscriptions, event callback and status callback
	app.subscriptions = args.events
	app.event_handler = event_handler
	app.status_handler = status_handler

	#Receive signals as events
	if (args.signals):
		app.enable_signals()

	ml.run()

def be_client(app, args):
	#Create timed event sender glib timeouts
	for event in args.events:
		sender_func = event_sender_factory(event, {"timed" : "event"}, send_handler_timed,
		                                   "timed event callback_data", \
										   label=args.label, appid=args.appid, \
										   binary=args.binary, user=args.user, \
										   process=args.process)
		glib.timeout_add(args.interval, sender_func, app, args.nevent)

	#Create burst sender glib timeouts
	for event in args.events:
		sender_func = event_sender_factory(event, ["this", "is", "an", "example", {"of" : "event data"}],
		                                   send_handler_burst, "burst event callback_data", \
		                                   label=args.label, appid=args.appid, \
										   binary=args.binary, user=args.user, \
										   process=args.process)
		glib.timeout_add(0, sender_func, app, args.burst)

	ml.run()

def main():
	#Parse arguments
	parser = argparse.ArgumentParser()
	parser.add_argument("-s", "--server", action="store_true", help="subscribe and wait for events")
	parser.add_argument("-l", "--label", type=str, help="target application label")
	parser.add_argument("-a", "--appid", type=str, help="target application id")
	parser.add_argument("-b", "--binary", type=str, help="target application binary path")
	parser.add_argument("-u", "--user", type=str, help="target application user")
	parser.add_argument("-p", "--process", type=int, help="target application process id")
	parser.add_argument("-e", "--events", type=str, help="events to send/subscribe for")
	parser.add_argument("-n", "--nevent", type=int, default=0, help="number of bundles to send")
	parser.add_argument("-S", "--signals", action="store_true", help="subscribe to system signals")
	parser.add_argument("-B", "--burst", type=int, default=0, help="number of events in initial burst")
	parser.add_argument("-I", "--interval", type=int, default=0, help=" between sending")
	parser.add_argument("-v", "--verbose", action="count", default=0, help="increase logging verbosity (ignored)")
	parser.add_argument("-d", "--debug", action="count", default=0, help="enable given debug configuration")
	args = parser.parse_args()
	args.events = args.events.split(',')

	#Create appfw context
	app = appfw.IotApp()

	if args.debug > 0:
		if args.debug >= 1:
			logging.basicConfig()
			logger.setLevel(logging.DEBUG)
		if args.debug >= 2:
			app._enable_debug([""])
		if args.debug >= 3:
			app._enable_debug(["@python-app-fw-wrapper.cpp"])
		if args.debug > 3:
			app._enable_debug(["*"])

	logger.debug(args)

	if args.server:
		be_server(app, args)
	else:
		be_client(app, args)

if __name__ == "__main__":
	main()
