#!/usr/bin/python
from glob import glob
from apport.hookutils import attach_file_if_exists, command_output

def add_info(report, ui):
    log_file = '/var/log/upstart/whoopsie.log'
    attach_file_if_exists(report, log_file, 'WhoopsieLog')

    reports = glob('/var/crash/*')
    if reports:
        report['CrashReports'] = command_output(
            ['stat', '-c', '%a:%u:%g:%s:%y:%x:%n'] + reports)

