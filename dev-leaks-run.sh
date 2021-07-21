#/bin/bash
# run with the macOS memory debugging options then use leaks tool to find memory leaks
kodev clean && kodev build
if [ $? -eq 0 ]; then
  MallocScribble=1 MallocPreScribble=1 kodev run
fi
