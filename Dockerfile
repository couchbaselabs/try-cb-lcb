FROM kore/kore:4.1.0

LABEL maintainer="Couchbase"

WORKDIR /koreapp

ADD . /koreapp

# Expose ports
EXPOSE 8080

# Set the entrypoint 
ENTRYPOINT ["./wait-for-couchbase.sh", "kore", "-c", "config/try-cb-lcb.config"]
