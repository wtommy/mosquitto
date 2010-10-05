#!/usr/bin/env python

import gnomeapplet
import gobject
import gtk
import mosquitto
import os
import platform
import pygtk
import sys

class CurrentCostMQTT(gnomeapplet.Applet):
	def loop(self):
		self.mosq.loop(1)
		return True

	def on_message(self, msg):
		# Message format is "power"
		self.label.set_text(msg.payload+"W")

	def set_label(self, val):
		self.label.set_text(val)

	def on_change_background(self, applet, type, color, pixmap):
		applet.set_style(None)
		applet.modify_style(gtk.RcStyle())

		if type == gnomeapplet.COLOR_BACKGROUND:
			applet.modify_bg(gtk.STATE_NORMAL, color)
		elif type == gnomeapplet.PIXMAP_BACKGROUND:
			style = applet.get_style().copy()
			style.bg_pixmap[gtk.STATE_NORMAL] = pixmap
			applet.set_style(style)

	def show_menu(self, widget, event):
		print "menu"
		
	def __init__(self, applet, iid):
		self.applet = applet
		self.label = gtk.Label("0W")
		self.event_box = gtk.EventBox()
		self.event_box.add(self.label)
		self.event_box.set_events(gtk.gdk.BUTTON_PRESS_MASK)
		self.event_box.connect("button_press_event", self.show_menu)
		self.applet.add(self.event_box)
		self.applet.set_background_widget(applet)
		self.applet.show_all()
		self.mosq = mosquitto.Mosquitto("ccpanel-"+platform.node()+"-"+str(os.getpid()))
		self.mosq.on_message = self.on_message
		self.mosq.connect("10.90.100.4", 1883, 60, True)
		self.mosq.subscribe("sensors/cc128/ch1", 0)
		self.applet.connect('change-background', self.on_change_background)
		gobject.timeout_add(100, self.loop)

def CurrentCostMQTT_factory(applet, iid):
	CurrentCostMQTT(applet, iid)
	return gtk.TRUE

if len(sys.argv) == 2:
    if sys.argv[1] == "-d": #Debug mode
        main_window = gtk.Window(gtk.WINDOW_TOPLEVEL)
        main_window.set_title("Python Applet")
        main_window.connect("destroy", gtk.main_quit)
        app = gnomeapplet.Applet()
        CurrentCostMQTT_factory(app,None)
        app.reparent(main_window)
        main_window.show_all()
        gtk.main()
        sys.exit()
 
if __name__ == '__main__':
	gnomeapplet.bonobo_factory("OAFIID:CurrentCostMQTT_Factory", gnomeapplet.Applet.__gtype__, "MQTT", "0", CurrentCostMQTT_factory)

