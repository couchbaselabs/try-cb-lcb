#/bin/bash

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

kodev clean && kodev build && kodev run
