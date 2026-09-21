#!/usr/bin/env python3
"""Validate manual, opt-in, or tag-push release requests against platformio.ini."""

import configparser
import json
import os
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TAG_PATTERN = re.compile(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)")


def resolve(event_name, event, root=ROOT):
    ref = os.environ.get("GITHUB_REF", "")
    if event_name == "workflow_dispatch":
        tag = event.get("inputs", {}).get("tag", "")
        enabled = True
    elif event_name == "push" and ref.startswith("refs/tags/"):
        # A version tag is itself a release request; do not require the JSON
        # request file or push another tag while processing it.
        tag = ref[len("refs/tags/"):]
        enabled = True
    elif event_name == "push" and ref == "refs/heads/master":
        request = json.loads((root / ".github" / "release-request.json").read_text(encoding="utf-8"))
        if set(request) != {"enabled", "tag"}:
            raise ValueError("Release request must contain exactly enabled and tag")
        enabled, tag = request["enabled"], request["tag"]
    else:
        raise ValueError(f"Unsupported release event/ref: {event_name} {ref}")

    if type(enabled) is not bool or not isinstance(tag, str):
        raise ValueError("Invalid release request field types")
    if not enabled:
        return {"publish": "false", "tag": "", "version": ""}
    if not TAG_PATTERN.fullmatch(tag):
        raise ValueError("Release tag must look like v1.2.3")
    if ref != "refs/heads/master" and ref != "refs/tags/" + tag:
        raise ValueError("GameBoy releases must be cut from master or a matching version tag")

    config = configparser.ConfigParser(interpolation=None)
    if not config.read(root / "platformio.ini"):
        raise ValueError("Missing platformio.ini")
    version = config.get("version", "version")
    if tag != "v" + version:
        raise ValueError(f"Requested tag {tag} does not match platformio.ini version {version}")

    if ref.startswith("refs/tags/"):
        # With checkout fetch-depth: 0, the tag's checked-out commit must be
        # contained in master; do not publish releases from arbitrary branches.
        ancestry = subprocess.run(
            ["git", "merge-base", "--is-ancestor", "HEAD", "origin/master"],
            cwd=root,
            check=False,
        )
        if ancestry.returncode == 1:
            raise ValueError(f"Tag {tag} does not point to a commit on master")
        if ancestry.returncode != 0:
            raise RuntimeError("Could not verify tag ancestry against origin/master")

    return {"publish": "true", "tag": tag, "version": version}


def main():
    event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text(encoding="utf-8"))
    outputs = resolve(os.environ["GITHUB_EVENT_NAME"], event)
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as stream:
        for key, value in outputs.items():
            stream.write(f"{key}={value}\n")
    print("GameBoy release request validated" if outputs["publish"] == "true" else "Release request disabled; no publication")


if __name__ == "__main__":
    main()
