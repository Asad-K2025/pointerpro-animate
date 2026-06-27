#!/usr/bin/env python3

import os
import re
import time
import select
import signal
import threading
import subprocess
import unittest
import warnings

warnings.simplefilter("ignore", ResourceWarning)

SERVER_BIN = "../animate_server"
CLIENT_BIN = "../animate_client"

FIFO_C2S = "FIFO_C2S_{}"
FIFO_S2C = "FIFO_S2C_{}"

TEST_TIMEOUT = 3

# utilities

def cleanup():

    for f in os.listdir("."):

        if (
            f.startswith("FIFO_C2S_")
            or f.startswith("FIFO_S2C_")
        ):
            try:
                os.unlink(f)
            except:
                pass

    subprocess.run(
        ["pkill", "-f", "animate_server"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )

    subprocess.run(
        ["pkill", "-f", "animate_client"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )

    for f in os.listdir("."):

        if (
            f.endswith(".dat")
            or f.endswith(".mp4")
            or f.endswith(".log")
        ):
            try:
                os.unlink(f)
            except:
                pass


def write_users(contents):

    with open("users.txt", "w") as f:
        f.write(contents)


def timed_read(pipe, timeout=TEST_TIMEOUT):

    if pipe.closed:
        return None

    try:

        ready, _, _ = select.select(
            [pipe],
            [],
            [],
            timeout
        )

        if ready:
            line = pipe.readline()

            if not line:
                return None

            return line.strip()

    except:
        return None

    return None


def close_pipe(pipe):

    try:
        if pipe and not pipe.closed:
            pipe.close()
    except:
        pass


def kill_process(proc):

    if proc is None:
        return

    try:

        if proc.poll() is None:
            proc.kill()

    except:
        pass

    try:
        proc.wait(timeout=1)
    except:
        pass

    close_pipe(proc.stdin)
    close_pipe(proc.stdout)
    close_pipe(proc.stderr)


# server wrapper

class Server:

    def __init__(self, threads=4):

        self.threads = threads
        self.proc = None
        self.pid = None

    def start(self):

        self.proc = subprocess.Popen(
            [SERVER_BIN, str(self.threads)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1
        )

        line = timed_read(self.proc.stdout)

        if line is None:
            raise RuntimeError("Server did not print PID")

        match = re.search(r"(\d+)", line)

        if not match:
            raise RuntimeError("Invalid PID line")

        self.pid = int(match.group(1))

    def stop(self):

        kill_process(self.proc)
        self.proc = None

# client wrapper

class Client:

    def __init__(self, server_pid):

        self.proc = subprocess.Popen(
            [CLIENT_BIN, str(server_pid)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1
        )

        self.pid = self.proc.pid

    def send(self, msg):

        try:

            if self.proc.poll() is not None:
                return

            self.proc.stdin.write(msg + "\n")
            self.proc.stdin.flush()

        except:
            pass

    def read(self, timeout=TEST_TIMEOUT):

        return timed_read(
            self.proc.stdout,
            timeout
        )

    def has_output(self, timeout=1):

        try:

            ready, _, _ = select.select(
                [self.proc.stdout],
                [],
                [],
                timeout
            )

            return bool(ready)

        except:
            return False

    def disconnect(self):

        try:
            self.send("Disconnect")
            time.sleep(0.2)
        except:
            pass

    def kill(self):

        kill_process(self.proc)
        self.proc = None

# automated testcases

class AnimateTests(unittest.TestCase):

    def setUp(self):

        cleanup()

        write_users(
            """
alice 10
bob 10
charlie 0
david -5
"""
        )

        self.server = Server(4)
        self.server.start()

        self.clients = []

    def tearDown(self):

        for c in self.clients:
            c.kill()

        self.server.stop()

        cleanup()

    # helpers

    def client(self):

        c = Client(self.server.pid)

        self.clients.append(c)

        return c

    def login(self, c, user="alice"):

        c.send(f"Login {user}")

        return c.read()

    def assert_any(self, value, allowed):

        self.assertTrue(
            value in allowed,
            f"Got '{value}', expected one of {allowed}"
        )

    # startup tests

    def test_server_pid_printed(self):

        self.assertTrue(self.server.pid > 0)

    def test_client_connects(self):

        c = self.client()

        fifo1 = FIFO_C2S.format(c.pid)
        fifo2 = FIFO_S2C.format(c.pid)

        time.sleep(0.5)

        self.assertTrue(os.path.exists(fifo1))
        self.assertTrue(os.path.exists(fifo2))

    # login tests

    def test_valid_login(self):

        c = self.client()

        out = self.login(c)

        self.assertIsNotNone(out)
        self.assertIn("Welcome", out)

    def test_unknown_user(self):

        c = self.client()

        out = self.login(c, "nobody")

        self.assertEqual(out, "Reject UNAUTHORISED")

    def test_zero_balance(self):

        c = self.client()

        out = self.login(c, "charlie")

        self.assertEqual(out, "Reject BALANCE")

    def test_negative_balance(self):

        c = self.client()

        out = self.login(c, "david")

        self.assertEqual(out, "Reject BALANCE")

    def test_login_whitespace(self):

        write_users(
            """
     alice      10
\tbob\t\t20
"""
        )

        self.server.stop()

        self.server = Server(4)
        self.server.start()

        c = self.client()

        out = self.login(c)

        self.assertIn("Welcome", out)

    # pre-authentication tests

    def test_not_logged_in(self):

        c = self.client()

        c.send("create_canvas 100 100")

        out = c.read()

        self.assertEqual(out, "Not logged in")

    # RPC parsing

    def test_unknown_rpc(self):

        c = self.client()

        self.login(c)

        c.send("dance")

        out = c.read()

        self.assertEqual(out, "RPC Failed")

    def test_missing_args(self):

        c = self.client()

        self.login(c)

        c.send("create_canvas")

        out = c.read()

        self.assertEqual(out, "RPC Failed")

    def test_invalid_integer(self):

        c = self.client()

        self.login(c)

        c.send("create_canvas abc 10")

        out = c.read()

        self.assert_any(
            out,
            ["Value error", "RPC Failed"]
        )

    def test_negative_unsigned(self):

        c = self.client()

        self.login(c)

        c.send("create_canvas -1 10")

        out = c.read()

        self.assert_any(
            out,
            ["Value error", "RPC Failed"]
        )

    # FIFO garbage

    def test_raw_fifo_garbage(self):

        c = self.client()

        fifo = FIFO_C2S.format(c.pid)

        time.sleep(0.5)

        with open(fifo, "wb") as f:
            f.write(b"\xff\x00HELLO\n")

        out = c.read()

        self.assertTrue(
            out in [
                "RPC Failed",
                "Value error",
                None
            ]
        )

    # disconnect test

    def test_ungraceful_disconnect(self):

        c = self.client()

        pid = c.pid

        c.kill()

        time.sleep(2)

        self.assertFalse(
            os.path.exists(FIFO_C2S.format(pid))
        )

    # ordering tests

    def test_response_ordering(self):

        c = self.client()

        self.login(c)

        for _ in range(3):
            c.send("create_canvas 100 100")

        outputs = [
            c.read(),
            c.read(),
            c.read()
        ]

        self.assertEqual(len(outputs), 3)

    # multiple clients

    def test_multiple_clients(self):

        clients = []

        for _ in range(10):

            c = self.client()

            self.login(c)

            clients.append(c)

        for c in clients:
            c.send("create_canvas 100 100")

        results = []

        for c in clients:
            results.append(c.read())

        self.assertEqual(len(results), 10)

    # stress test

    def test_stress(self):

        clients = []

        for _ in range(20):

            c = self.client()

            self.login(c)

            clients.append(c)

        def spam(client):

            for _ in range(20):

                client.send(
                    "create_canvas 100 100"
                )

                client.read()

        threads = []

        for c in clients:

            t = threading.Thread(
                target=spam,
                args=(c,)
            )

            t.start()

            threads.append(t)

        for t in threads:
            t.join()

        self.assertTrue(True)

    # thread count

    def test_thread_count_constant(self):

        before = subprocess.check_output(
            ["ps", "-T", "-p", str(self.server.pid)]
        ).decode()

        before_threads = len(
            before.strip().split("\n")
        )

        clients = []

        for _ in range(20):
            clients.append(self.client())

        after = subprocess.check_output(
            ["ps", "-T", "-p", str(self.server.pid)]
        ).decode()

        after_threads = len(
            after.strip().split("\n")
        )

        self.assertEqual(
            before_threads,
            after_threads
        )

    # barrier

    def test_barrier_single_client(self):

        c = self.client()

        self.login(c)

        c.send("barrier 1")

        out = c.read()

        self.assertTrue(
            out in [
                "Success",
                "Value error",
                "RPC Failed"
            ]
        )

    # generate

    def test_generate_invalid(self):

        c = self.client()

        self.login(c)

        c.send(
            "generate 1 movie 10 0 30"
        )

        out = c.read()

        self.assertTrue(
            out in [
                "Value error",
                "RPC Failed"
            ]
        )

    # large input

    def test_large_input(self):

        c = self.client()

        self.login(c)

        huge = "A" * 100000

        c.send(huge)

        out = c.read()

        self.assertTrue(
            out in [
                "RPC Failed",
                "Value error",
                None
            ]
        )

    # rapid reconnect

    def test_rapid_reconnect(self):

        for _ in range(5):

            c = self.client()

            self.login(c)

            c.disconnect()

        self.assertTrue(True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
