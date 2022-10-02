import pytest
import requests


def pytest_addoption(parser):
    parser.addoption("--base_url", action="store", default="http://localhost:8080")
    parser.addoption("--httpserver_host", action="store", default="host.docker.internal")
    parser.addoption("--httpserver_listen_address", action="store", default="127.0.0.1")
    parser.addoption("--rascsi_username", action="store", default="pi")
    parser.addoption("--rascsi_password", action="store", default="rascsi")


@pytest.fixture(scope="session")
def httpserver_listen_address(pytestconfig):
    return (pytestconfig.getoption("httpserver_listen_address"), 0)


@pytest.fixture(scope="function", autouse=True)
def set_httpserver_hostname(pytestconfig, httpserver):
    # The HTTP requests are made by Python from within the container so we need
    # httpserver.url_for to generate URLs which point to the Docker host
    httpserver.host = pytestconfig.getoption("httpserver_host")


@pytest.fixture(scope="session", autouse=True)
def ensure_all_devices_detached(create_http_client):
    http_client = create_http_client()
    http_client.post("/scsi/detach_all")


@pytest.fixture(scope="session")
def create_http_client(pytestconfig):
    def create(authenticate=True):
        session = requests.Session()
        session.headers.update({"Accept": "application/json"})
        session.original_request = session.request

        def relative_request(method, url, *args, **kwargs):
            if url[:4] != "http":
                url = pytestconfig.getoption("base_url") + url

            return session.original_request(method, url, *args, **kwargs)

        session.request = relative_request

        if authenticate:
            session.post(
                "/login",
                data={
                    "username": pytestconfig.getoption("rascsi_username"),
                    "password": pytestconfig.getoption("rascsi_password"),
                },
            )
        return session

    return create


@pytest.fixture(scope="function")
def http_client(create_http_client):
    return create_http_client(authenticate=True)


@pytest.fixture(scope="function")
def http_client_unauthenticated(create_http_client):
    return create_http_client(authenticate=False)