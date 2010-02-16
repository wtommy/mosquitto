#!/usr/bin/perl
use strict;
use Net::Pachube;

# Update a pachube feed.
# Run as "pachube_update.pl <feed_id>", where <feed_id> is the id of your feed,
# e.g. xxxx from http://www.pachube.com/api/feeds/xxxx.xml
# Your pachube api key should be set in the environment variable
# PACHUBE_API_KEY
#
# Use with mosquitto_sub like:
#
# mosquitto_sub -h <host> -i <id> -t <topic> | pachube_update.pl <feed_id>
#
# Needs Net::Pachube installing from CPAN.

if($#ARGV != 0){
	print "Usage: pachube_update.pl <feed_id>\n";
	exit
}

my $feed_id = $ARGV[0];

my $pachube = Net::Pachube->new();
my $feed = $pachube->feed($feed_id);

#print $feed->title, " ", $feed->status, "\n";

while(<STDIN>){
	chomp;
	$_ += 0;
	$feed->update(data => $_);
}
