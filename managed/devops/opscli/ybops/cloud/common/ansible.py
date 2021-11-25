#!/usr/bin/env python
#
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt


import json
import logging
import os
import subprocess
import sysconfig

from ybops.common.exceptions import YBOpsRuntimeError
import ybops.utils as ybutils


class AnsibleProcess(object):
    """Generic class for handling external calls to our Ansible playbooks.

    Callers can directly update self.playbook_args before calling the run() method.
    """
    DEFAULT_SSH_USER = "centos"
    DEFAULT_SSH_CONNECTION_TYPE = "ssh"
    REDACT_STRING = "REDACTED"

    def __init__(self):
        self.yb_user_name = "yugabyte"

        self.playbook_args = {
            "user_name": self.yb_user_name,
            "instance_search_pattern": "all"
        }

        self.can_ssh = True
        self.connection_type = self.DEFAULT_SSH_CONNECTION_TYPE
        self.connection_target = "localhost"
        self.sensitive_data_keywords = ["KEY", "SECRET", "CREDENTIALS", "API", "POLICY"]

    def set_connection_params(self, conn_type, target):
        self.connection_type = conn_type
        self.connection_target = self.build_connection_target(target)

    def build_connection_target(self, target):
        return target + ","

    def is_sensitive(self, key_string):
        for word in self.sensitive_data_keywords:
            if word in key_string:
                return True
        return False

    def redact_sensitive_data(self, playbook_args):
        for key, value in playbook_args.items():
            if self.is_sensitive(key.upper()):
                playbook_args[key] = self.REDACT_STRING
        return playbook_args

    def run(self, filename, extra_vars=dict(), host_info={}, print_output=True):
        """Method used to call out to the respective Ansible playbooks.
        Args:
            filename: The playbook file to execute
            extra_args: A dictionary of KVs to pass as extra-vars to ansible-playbook
            host_info: A dictionary of host level attributes which is empty for localhost.
        """

        playbook_args = self.playbook_args
        vars = extra_vars.copy()
        tags = vars.pop("tags", None)
        skip_tags = vars.pop("skip_tags", None)
        # Use the ssh_user provided in extra vars as the ssh user to override.
        ssh_user = vars.pop("ssh_user", host_info.get("ssh_user", self.DEFAULT_SSH_USER))
        ssh_port = vars.pop("ssh_port", None)
        ssh_host = vars.pop("ssh_host", None)
        vault_password_file = vars.pop("vault_password_file", None)
        ask_sudo_pass = vars.pop("ask_sudo_pass", None)
        sudo_pass_file = vars.pop("sudo_pass_file", None)
        ssh_key_file = vars.pop("private_key_file", None)

        playbook_args.update(vars)

        if self.can_ssh:
            playbook_args.update({
                "ssh_user": ssh_user,
                "yb_server_ssh_user": ssh_user
            })

        playbook_args["yb_home_dir"] = ybutils.YB_HOME_DIR

        process_args = [
            "ansible-playbook", os.path.join(ybutils.YB_DEVOPS_HOME, filename)
        ]

        if vault_password_file is not None:
            process_args.extend(["--vault-password-file", vault_password_file])
        if ask_sudo_pass is not None:
            process_args.extend(["--ask-sudo-pass"])
        if sudo_pass_file is not None:
            playbook_args["yb_sudo_pass_file"] = sudo_pass_file

        if skip_tags is not None:
            process_args.extend(["--skip-tags", skip_tags])
        elif tags is not None:
            process_args.extend(["--tags", tags])

        if ssh_port is None or ssh_host is None:
            connection_type = "local"
            inventory_target = "localhost,"
        elif self.can_ssh:
            process_args.extend([
                "--private-key", ssh_key_file,
                "--user", ssh_user
            ])

            playbook_args.update({
                "yb_ansible_host": ssh_host,
                "ansible_port": ssh_port
            })

            inventory_target = self.build_connection_target(ssh_host)
            connection_type = self.DEFAULT_SSH_CONNECTION_TYPE
        else:
            connection_type = self.connection_type
            inventory_target = self.build_connection_target(
                host_info.get("name", self.connection_target))

        # Set inventory, connection type, and pythonpath.
        process_args.extend([
            "-i", inventory_target,
            "-c", connection_type,
        ])

        redacted_process_args = process_args.copy()

        # Setup the full list of extra-vars needed for ansible plays.
        process_args.extend(["--extra-vars", json.dumps(playbook_args)])
        redacted_process_args.extend(
            ["--extra-vars", json.dumps(self.redact_sensitive_data(playbook_args))])
        env = os.environ.copy()
        if env.get('APPLICATION_CONSOLE_LOG_LEVEL') != 'INFO':
            env['PROFILE_TASKS_TASK_OUTPUT_LIMIT'] = '30'
        logging.info("[app] Running ansible playbook {} against target {}".format(
                        filename, inventory_target))
        logging.info("Running ansible command {}".format(json.dumps(redacted_process_args,
                                                                    separators=(' ', ' '))))
        p = subprocess.Popen(process_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        stdout, stderr = p.communicate()
        if print_output:
            print(stdout.decode('utf-8'))
        EXCEPTION_MSG_FORMAT = ("Playbook run of {} against {} with args {} " +
                                "failed with return code {} and error '{}'")
        if p.returncode != 0:
            raise YBOpsRuntimeError(EXCEPTION_MSG_FORMAT.format(
                    filename, inventory_target, redacted_process_args, p.returncode, stderr))
        return p.returncode
