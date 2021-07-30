#/bin/bash

# IMPORTANT NOTES:
# - you must have previously built the project
# - drop the -n and -r options for a real production server

check_env () {
  env_var=$1
  if [[ -z "${!env_var}" ]]; then
    echo "WARNING: $env_var is not set (default will be used)"
  fi
}

check_env "CB_SCHEME"
check_env "CB_HOST"
check_env "CB_USER"
check_env "CB_PSWD"

kore -n -r -c "$PWD/conf/try-cb-lcb.conf"
