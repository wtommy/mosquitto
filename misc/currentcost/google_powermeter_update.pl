#!/usr/bin/perl

use strict;
use LWP::UserAgent;
use POSIX qw(strftime);
use DBI();
use Data::Dumper;

# userID is your 20-digit google powermeter id
my $userID = "";

# variableID is the combination of device manufacturer, device model and device
# id and variable name, as when you activated the device.
# Separate the fields with a full stop: mfg.model.id.varname e.g. CurrentCost.CC128.1234.d1
# If you have more than one channel, you'll need to modify the whole script I'm afraid!
# This code assumes you're using one dvar.
my $variableID = "CurrentCost.CC128.1234.d1";

# The auth token you received on activating your device (32 chars)
my $authtoken = "";

# Add your database details
my $dbname = "powermeter";
my $dbhost = "localhost";
my $dbusername = "powermeter";
my $dbpassword = "";
my $dbtable = "powermeter";

# You shouldn't need to edit anything else below.

my $subjecturl = "https://www.google.com/powermeter/feeds/user/$userID/$userID/variable/$variableID";
my $posturl = "https://www.google.com/powermeter/feeds/event";
my $ua = LWP::UserAgent->new;
my $req = HTTP::Request->new(POST => $posturl);
$req->header('Authorization' => "AuthSub token=\"$authtoken\"");
$req->content_type('application/atom+xml');

my $dbh = DBI->connect("DBI:mysql:database=$dbname;host=$dbhost",
		"$dbusername", "$dbpassword", {'RaiseError' => 1});

# Send 15 minutes + 2 lots of readings
my $now = time - 912;
my $query = "SELECT * FROM $dbtable WHERE timestamp > $now ORDER BY timestamp LIMIT 152";
my $sth = $dbh->prepare($query);
my $rc = $sth->execute();

my $msg = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n".
		"<feed xmlns=\"http://www.w3.org/2005/Atom\"\n".
		"      xmlns:meter=\"http://schemas.google.com/meter/2008\"\n".
		"      xmlns:batch=\"http://schemas.google.com/gdata/batch\">\n";

my $batchid = 1;
while(my $ref = $sth->fetchrow_hashref()){
	my $timestamp = $ref->{'timestamp'};
	my $reading_time = strftime("%Y-%m-%dT%H:%M:%S.000Z", gmtime($timestamp));
	my $power = $ref->{'ch1'};

	my $kwh = ($power/600)/1000; # http://currentcost.posterous.com/calculating-kwh-from-watts

	$msg = $msg . "<entry>\n".
			"<category scheme=\"http://schemas.google.com/g/2005#kind\"".
			" term=\"http://schemas.google.com/meter/2008#durMeasurement\"/>\n".
			"<meter:subject>$subjecturl</meter:subject>\n".
			"<batch:id>A.$batchid</batch:id>\n".
			"<meter:startTime meter:uncertainty=\"1.0\">$reading_time</meter:startTime>\n".
			"<meter:duration meter:uncertainty=\"0.1\">6.0</meter:duration>\n".
	    		"<meter:quantity meter:uncertainty=\"0.001\" meter:unit=\"kW h\">$kwh</meter:quantity>\n".
			"</entry>\n";
	$batchid++;
}
$sth->finish();
$dbh->disconnect();

$msg = $msg . "</feed>\n";

$req->content($msg);
my @res = $ua->request($req);
print Dumper(@res);

