import argparse
import glib
import json
import logging
import appfw

logger = logging.getLogger("__event-catch__")

quit_event = None

def event_cb(event, data):
	logger.debug("Received event <" + event + ">")
	logger.debug("  with data " + str(data))
	if event == quit_event:
		ml.quit() #This is the correct way to quit an application

def subscribe_status(seqno, status, msg, data, user_data):
	if status == 0:
		logger.debug("Succesfully subscribed for events.")
	else:
		if msg == None:
			msg = "<unknown error>"
		logger.debug("Event subscription failed (" + str(status) + ": " + \
		             msg + ").")

def main():
	#Parse arguments
	parser = argparse.ArgumentParser()
	parser.add_argument("-e", "--events", type=str , help="events to send/subscribe for")
	parser.add_argument("-q", "--quit", type=str,  default=None, help="last event to send")
	parser.add_argument("-s", "--signals", action="store_true", help="bridge system signals as events")
	parser.add_argument("-v", "--verbose", action="count", default=0, help="increase logging verbosity (ignored)")
	parser.add_argument("-d", "--debug", action="count", default=0, help="enable given debug configuration")
	parser.add_argument("-G", "--glib", action="store_true", default=True, help="User glib mainloop (ignored, python always uses Glib mainloop)")
	args = parser.parse_args()
	args.events = args.events.split(',')

	#Create mainloop to default context
	ml = glib.MainLoop(None)

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

	#Set the status callback
	app.status_handler = subscribe_status

	#This information can't be delivered to the event callback otherwise
	global quit_event
	quit_event = args.quit

	if (args.signals):
		app.enable_signals()

	#Set event subscriptions
	app.subscriptions = set(args.events)

	#Set event callback
	app.event_handler = event_cb

	#Start mainloop
	ml.run()

if __name__ == "__main__":
	main()