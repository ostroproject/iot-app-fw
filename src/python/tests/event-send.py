import argparse
import glib
import json
import logging
import appfw

logger = logging.getLogger("__event-send__")

def send_status(id, status, msg, callback_data):
	if status == 0:
		logger.debug("Event request #" + str(id) + " succesfully delivered.")
	else:
		if msg == None:
			msg = "<unknown error>"
		logger.debug("Event request #" + str(id) + "failed (" + \
		             str(id) + ": " + msg)

def event_sender(app, args):
	event = args.events[event_sender.counter % len(args.events)]
	if event_sender.counter == args.nevent and args.quit != None:
		event = args.quit

	if event_sender.counter <= args.nevent:
		event_data = args.data
		#Append counter if data is a dict
		if isinstance(args.data, dict):
			event_data['count'] = event_sender.counter

		logger.debug("Sending event " + str(event) + " to { " + \
		             str(args.label) + "," + str(args.appid) + "," + \
		             str(args.binary) + ", user " + str(args.user) + \
		             ", pid " + str(args.process) + " }")
		logger.debug("  with data " + json.dumps(event_data))

		app.send_event(event, event_data, send_status, None, label=args.label,
		               appid=args.appid, binary=args.binary, 
		               user=args.user, process=args.process)
		event_sender.counter += 1
		return True
	return False
event_sender.counter = 1

def add_timed_event(app, args):
	glib.timeout_add(args.interval, event_sender, app, args)

def main():
	#Parse arguments
	parser = argparse.ArgumentParser()
	parser.add_argument("-l", "--label", type=str, help="target application label")
	parser.add_argument("-a", "--appid", type=str, help="target application id")
	parser.add_argument("-b", "--binary", type=str, help="target application binary path")
	parser.add_argument("-u", "--user", type=str, help="target application user")
	parser.add_argument("-p", "--process", type=int, help="target application process id")
	parser.add_argument("-e", "--events", type=str, help="events to send/subscribe for")
	parser.add_argument("-q", "--quit", type=str,  default=None, help="last event to send")
	parser.add_argument("-D", "--data", type=json.loads, help="data to attach to events")
	parser.add_argument("-n", "--nevent", type=int, default=0, help="number of events to send")
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

	#Pick the last event to send
	if args.quit == None and len(args.events) > 1:
		args.quit = args.events[-1]
		del args.events[-1]

	#Create mainloop to default context
	ml = glib.MainLoop(None)

	#Add event senders
	add_timed_event(app, args)
	
	#Start mainloop
	ml.run()

if __name__ == "__main__":
	main()