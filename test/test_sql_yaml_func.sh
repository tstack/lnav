#! /bin/bash

export YES_COLOR=1

run_cap_test ${lnav_test} -n -c ";SELECT yaml_to_json('[abc')"
