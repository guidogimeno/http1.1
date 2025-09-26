#!/bin/bash

# Array of curl commands
commands=(
    "curl -X GET http://localhost:8080/"
    "curl -X GET http://localhost:8080/foo"
    "curl -X GET http://localhost:8080/3333"
    "curl -X GET http://localhost:8080/foo/"
    "curl -X GET http://localhost:8080/1234/"
    "curl -X GET http://localhost:8080/foo/1234"
    "curl -X GET http://localhost:8080/1234/baz"
    "curl -X GET http://localhost:8080/1234/4444"
    "curl -X GET http://localhost:8080/foo/bar/baz/"
    "curl -X GET http://localhost:8080/1234/bar/910/"
    "curl -X GET http://localhost:8080/1234/5678/910/"
    "curl -X GET http://localhost:8080/foo/1234/baz"
)

# Log file
log_file="curl_responses.log"

# Clear log file if it exists
> "$log_file"

# Loop through commands and execute them
for i in "${!commands[@]}"; do
    echo "Executing command $((i+1)): ${commands[i]}" >> "$log_file"
    echo "Response for command $((i+1)):" >> "$log_file"
    # Execute curl and append response to log
    eval "${commands[i]}" >> "$log_file" 2>&1
    echo -e "\n---\n" >> "$log_file"
done

echo "Batch test completed. Responses logged in $log_file"
