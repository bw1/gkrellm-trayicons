#!/usr/bin/perl

# Notification script for irssi and gkrellm-trayicons.
#
#   mkdir ~/.trayicons
#   
#   mkdir ~/.trayicons/1
#   mkdir ~/.trayicons/2
#   mkdir ~/.trayicons/3
#   ...
#
# Configure gkrellm-trayicons to use ~/.trayicons/X as 
# activation directories.
#
# Example irssi settings:
# 
#   traylet_active = ON
#   traylet_hilight_regexp = peter.*|george.*
#   traylet_watch_nicks = thomas:2 simon:2 alice:3 michael:3 john:4
#
# Author:
# Tomas Styblo <tripie@cpan.org>

use strict;
use Irssi;
use Irssi::Irc;
use Data::Dumper;

use vars qw($VERSION %IRSSI);
$VERSION = "0.2";
%IRSSI = (
    authors => 'Tomas Styblo',
    contact => 'tripie@cpan.org',
    name => 'trayicons notify script',
    name => 'trayicons notify script',
    licence => 'GPL',
);

my $BASEDIR = $ENV{"HOME"}."/.trayicons/";
my $TRAYLET_PRIVATE_MSG = "$BASEDIR/1";
my $TRAYLET_HILIGHT_CHANNEL = "$BASEDIR/1";
my $TRAYLET_TOPIC = "$BASEDIR/2";

my $LAST_MSG_PRIVATE_TIME = time();
my $LAST_MSG_CONTENT = "";

sub event_message_private {
    my ($server, $msg, $nick, $address) = @_;
   
    # my $tooltip = "MSG: $nick($address): $msg";
    my $tooltip = "MSG: $nick($address)";
    
    if (Irssi::settings_get_bool("traylet_active") &&
        (time() - $LAST_MSG_PRIVATE_TIME >= 2 || $tooltip ne $LAST_MSG_CONTENT)) {
        &traylet_notify($TRAYLET_PRIVATE_MSG, $tooltip);
        $LAST_MSG_PRIVATE_TIME = time();
        $LAST_MSG_CONTENT = $tooltip;
    }
}

sub event_message_public {
    my ($server, $msg, $nick, $address, $target) = @_;
    
    if (Irssi::settings_get_bool("traylet_active")) {
        my $hilight_regexp = Irssi::settings_get_str("traylet_hilight_regexp");
        if (length($hilight_regexp) > 0 && $msg =~ /$hilight_regexp/i) {
            # my $tooltip = "HILIGHT: $nick($address): $target [$msg]";
            my $tooltip = "HILIGHT: $nick($address): $target";
            &traylet_notify($TRAYLET_HILIGHT_CHANNEL, $tooltip);
        }
    }
}

sub event_topic {
    my ($server, $channel, $topic, $nick, $address) = @_;
    
    if (Irssi::settings_get_bool("traylet_active")) {
        my $tooltip = "TOPIC: $nick($address): $channel [$topic]";
        &traylet_notify($TRAYLET_TOPIC, $tooltip);
    }
}

sub in_watch {
    my ($nick, $tray_ref) = @_;
    
    my @watch_nicks = split(/\s+/, Irssi::settings_get_str("traylet_watch_nicks"));
    my $traylet = undef;
    foreach my $watch (@watch_nicks) {
        if ($watch =~ /(.+):(\d+)/) {
            $watch = $1;
            $traylet = $2;
        }
        
        if ($nick =~ /$watch.*/i) {
            if (defined($traylet)) {
                ${$tray_ref} = "$BASEDIR/$traylet";
            }
            return 1;
        }
    }
    
    return 0;
}

sub event_join {
    my ($server, $channel, $nick, $address) = @_;
    
    if (Irssi::settings_get_bool("traylet_active")) {
        my $traylet = undef;
        if (&in_watch($nick, \$traylet)) {
            my $tooltip = "JOIN: $nick($address): $channel";
            &traylet_notify($traylet, $tooltip);
        }
    }
}

sub event_part {
    my ($server, $channel, $nick, $address, $reason) = @_;
    
    if (Irssi::settings_get_bool("traylet_active")) {
        my $traylet = undef;
        if (&in_watch($nick, \$traylet)) {
            my $tooltip = "PART: $nick($address) $channel [$reason]";
            &traylet_notify($traylet, $tooltip);
        }
    }
}

sub event_quit {
    my ($server, $nick, $address, $reason) = @_;
    
    if (Irssi::settings_get_bool("traylet_active")) {
        my $traylet = undef;
        if (&in_watch($nick, \$traylet)) {
            my $tooltip = "QUIT: $nick($address) $reason";
            &traylet_notify($traylet, $tooltip);
        }
    }
}

sub event_kick {
    my ($server, $channel, $nick, $kicker, $address, $reason) = @_;
    
    if (Irssi::settings_get_bool("traylet_active")) {
        my $traylet = undef;
        if (&in_watch($nick, \$traylet)) {
            my $tooltip = "KICK: $nick($address) $channel [$kicker\:$reason]";
            &traylet_notify($traylet, $tooltip);
        }
    }
}

sub event_nl_joined {
    my ($server, $nick, $user, $host, $realname, $awaymsg) = @_;
    
    if (Irssi::settings_get_bool("traylet_active")) {
        my $traylet = undef;
        if (&in_watch($nick, \$traylet)) {
            my $tooltip = "IRC JOIN: $nick($user\@$host) $realname";
            &traylet_notify($traylet, $tooltip);
        }
    }
}

sub traylet_notify {
    my ($traylet, $tooltip) = @_;
    my $unique_filename = sprintf("%d.%d.%d", time(), $$, int(rand(999999)));
    my $traylet_filename = "$traylet/$unique_filename";
    if (! open(TRAYLET, ">$traylet_filename")) {
        &error("Cannot write to traylet file ($traylet_filename): $!");
    }
    print TRAYLET "$tooltip\n";
    if (! close(TRAYLET)) {
        &error("Cannot close traylet file ($traylet_filename): $!");
    }
}
    
sub error {
    my ($msg) = @_;
    Irssi::print("Traylet notify error: $msg");
}

Irssi::settings_add_str("misc", "traylet_watch_nicks", "");
Irssi::settings_add_str("misc", "traylet_hilight_regexp", "");
Irssi::settings_add_bool("misc", "traylet_active", 1);

Irssi::signal_add("message private", "event_message_private");
Irssi::signal_add("message join", "event_join");
Irssi::signal_add("message part", "event_part");
Irssi::signal_add("message kick", "event_kick");
Irssi::signal_add("message quit", "event_quit");
Irssi::signal_add("message topic", "event_topic");
Irssi::signal_add("message public", "event_message_public");

Irssi::signal_add("notifylist joined", "event_nl_joined");
