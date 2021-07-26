# Couchbase LCB travel-sample Application REST Backend

This is a sample application for getting started with [Couchbase Server] and the [C-SDK] (aka, [libcouchbase] or **"LCB"**).
The application runs a single page web UI for demonstrating [N1QL] (SQL for Documents), [Sub-Document] requests, and Full Text Search ([FTS]) querying capabilities.
It uses **Couchbase Server** together with the [Kore.io] web framework, [Swagger] for API documentation, [cJSON] for handling JSON serialization, and some other utility libraries.

The application is a flight planner that demonstrates: new user registration, returning user login, searching for and selecting flight routes based on airports and dates, adding user bookings, displaying user bookings, and searching for hotels.

![Application](screenshot.png)

-----


## Design Notes

This sample is focused on demonstrating some of the basic concepts of working with the Couchbase [C-SDK] and does not represent best practices for writing a scalable production ready REST server. [Kore.io] has several features that can be used to implement strategies that scale much better (memory pools, async tasks, etc). Likewise, [libcouchbase] has several strategies that can be used to help orchestrate asynchronous responses or events. However, this sample currently just implements a simple "thread per connection" model by using one LCB instance per thread, thread local memory, and `lcb_wait()` with synchronous callback delegates. A scalable production server would most likely want to explore the fully asynchronous strategies instead.

### Server Layer Components

There are three server component layers required to run the full application:
- `db` - The Couchbase Server database used as the storage layer.
- `backend` - This project which implements a REST API that is used to communicate with the `db` storage layer.
- `frontend` - The web application that calls into the `backend` REST API backend to access the storage layer.


## Dependencies

This project has the following direct dependencies (which may pull in transitive dependencies):

- [libcouchbase] - C library SDK for Couchbase.
- [libjwt] - C library for encoding JSON Web Tokens.
- [libuuid] - C library for generating UUIDs.
- [Kore.io] - An HTTP server framework written in C.
- [cJSON] - C library for serializing JSON data (source copied here).

The following are indirect dependencies or tools that may be useful for development:
- [Swagger] - Language for describing a REST API.
- [Docker] - Provides containers to help get up and running quickly.
- [Visual Studio Code] - IDE with support for C/C++ and many other technologies.

-----


## Getting Started

To download the application you can either [download the archive](https://github.com/couchbaselabs/try-cb-lcb/archive/master.zip) or clone the repository:

```
git clone https://github.com/couchbaselabs/try-cb-lcb.git
```

We would normally suggest [Running with Docker Compose](#running-with-docker-compose) or using the [Mix and Match Services](#mix-and-match-services) to get up an running quickly without worrying about all the details. However, the Docker configuration for this project _**does not currently work**_ until we resolve some issues that were recently discovered.

You may also want to consider [Running All Components Manually](#running-all-components-manually) if you need more control or to help get setup for a more active development workflow without using Docker. _**This is currently the only supported option until we resolve the aforementioned Docker issues.**_


## Running with Docker Compose

> WARNING: This is a new repository and the Docker configuration needs more work. Do not use any Docker instructions for this project until the issues are fixed and this comment is removed.

You will need [Docker] installed on your machine in order to run this application as we have defined a [_Dockerfile_](Dockerfile) and a [_docker-compose.yml_](docker-compose.yml) to orchestrate all three of the [Server Layer Components](#server-layer-components).

Running with Docker Compose is as simple as running:

```
docker-compose up
```

> **_NOTE:_** When you run the application for the first time, it will pull/build the relevant Docker images, so it might take a bit longer than usual.

Once everything is up and running, you can access the `backend` server REST API on `http://localhost:8080/`, the `frontend` server web application on `http://localhost:8081/` and the `db` server Couchbase Server at `http://localhost:8091/`.

Using the web application, you can then register a user account, login, and exercise all of the features of the application.

To end the application press <kbd>Control</kbd>+<kbd>C</kbd> in the terminal and wait for docker-compose to gracefully stop your containers.


## Mix and Match Services

> WARNING: This is a new repository and the Docker configuration needs more work. Do not use any Docker instructions for this project until the issues are fixed and this comment is removed.

Instead of running all services, you can start any combination of `backend`,`frontend`, `db` via Docker, and take responsibility for starting the other services yourself.

As the provided [`docker-compose.yml`](docker-compose.yml) sets up dependencies between the services, to make startup as smooth and automatic as possible, we also provide an alternative [`mix-and-match.yml`](mix-and-match.yml).  We'll look at a few useful scenarios here.


### Bring your own database

If you wish to run this application against your own configuration of Couchbase Server, you will need version 7.0.0 GA or later with the `travel-sample` sample bucket installed and the `hotels-index` created.

> **_NOTE:_** If you are not using Docker to start up the Database, or the provided wrapper [`wait-for-couchbase.sh`](wait-for-couchbase.sh), you will need to create a full text search index on the `travel-sample` bucket called `hotels-index`. You can do this with the following command:

```
curl --fail -s -u <username>:<password> -X PUT \
        http://<host>:8094/api/index/hotels-index \
        -H 'cache-control: no-cache' \
        -H 'content-type: application/json' \
        -d @fts-hotels-index.json
```

Once you have a Couchbase Server running, the `travel-sample` sample data installed, and the `hotels-index` created, you can start the `frontend` and `backend` by passing the Couchbase server connection params as environment variables:

```
CB_HOST=cb.example.com CB_USER=Administrator CB_PSWD=password docker-compose -f mix-and-match.yml up backend frontend
```

### Running the backend manually

If you want to run the `backend` REST server (this project) yourself without using Docker, you will need to ensure that you have the dependencies installed and are able to run the [Kore.io] run commands. You may still use Docker to run the `db` and `frontend` components if desired. The information in [Running All Components Manually](#running-all-components-manually) may be useful in this regard.

For example, if you want to see how the sample frontend Vue application works with your manually running `backend` changes, you can just run the `frontend` with:

```
docker-compose -f mix-and-match.yml up frontend
```

### Running the frontend manually

To run the `frontend` web application manually without Docker, follow the instructions in the [front end repository](https://github.com/couchbaselabs/try-cb-frontend-v2). It's a `node.js` based project and the setup is fairly easy.

-----


## Running All Components Manually

When actively developing the `backend` logic, it's often more convenient to run all components manually when you need more direct control, or if any problems arise with the Docker images.

For local development, the instructions mentioned in [Mix and Match Services](#mix-and-match-services) provide all of the information required for the `db` and `frontend` server components. This section will focus on what's required for this project (the `backend` server component).

### Local Development Setup

There are a few libraries required, but [Kore.io] is the most limiting dependency. Kore supports several unix variants but it's easiest to get started on **macOS 10.10.x** or greater. So using [homebrew](https://brew.sh/) on macOS is the easiest way to get up and running with the following command:

```
brew install libcouchbase libjwt ossp-uuid kore
```

### Starting the Backend

See [Kore.io] documentation for more details on various ways to run the server. However, during development, the most useful way to run the server in DEBUG mode is using the `kodev` command. For convenience, we can just use the included helper script [run-dev.sh](./run-dev.sh). However, Kore takes control of the command line arguments, so we must specify the Couchbase Server configuration parameters using environment variables instead.

For example, the following commmand will run the `backend` server as a foreground process in DEBUG mode all log output will be visible in the terminal console:

```
CB_HOST=127.0.0.1 CB_USER=raycardillo CB_PSWD=raycardillo ./dev-run.sh
```

To stop the server press <kbd>Control</kbd>+<kbd>C</kbd> in the terminal and wait for the server to gracefully shutdown.

### Important Reminders

- Verify that the `db` is installed as described in the [Bring your own database](#bring-your-own-database) section above and is up and running without errors.
- Verify that the `frontend` is setup, installed, and running  as described in the [try-cb-frontend-v2](https://github.com/couchbaselabs/try-cb-frontend-v2) repository instructions.


-----


## REST API Swagger Specification

The Swagger (OpenApi version 3) specification document can be accessed on the backend at `http://localhost:8080/apidocs` or in the [assets directory](./assets).


[Couchbase Server]: https://www.couchbase.com/
[C-SDK]: https://docs.couchbase.com/c-sdk/current/hello-world/overview.html
[libcouchbase]: https://github.com/couchbase/libcouchbase
[Sub-Document]: https://docs.couchbase.com/c-sdk/current/howtos/subdocument-operations.html
[N1QL]: https://docs.couchbase.com/c-sdk/current/howtos/n1ql-queries-with-sdk.html
[FTS]: https://docs.couchbase.com/c-sdk/current/howtos/full-text-searching-with-sdk.html
[Kore.io]: https://kore.io/
[cJSON]: https://github.com/DaveGamble/cJSON
[libjwt]: https://github.com/benmcollins/libjwt
[libuuid]: http://www.ossp.org/pkg/lib/uuid/
[Swagger]: https://swagger.io/resources/open-api/
[Docker]: https://docs.docker.com/get-docker/
[Visual Studio Code]: https://code.visualstudio.com/