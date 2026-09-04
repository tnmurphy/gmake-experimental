from dataclasses import dataclass
from typing import Optional, Dict, Any, List
from unittest import TestCase


@dataclass
class Variable:
    name: str
    origin: str
    private: bool
    assign: Optional[str] = None
    assign_recursive: Optional[str] = None

    @classmethod
    def from_dict(cls, key: str, data: dict[str, Any]) -> "Variable":
        """Factory function to create a Variable from a dictionary."""
        return cls(
            name=key,
            origin=data["origin"],
            private=data["private"],
            assign=data.get("assign"),
            assign_recursive=data.get("assign-recursive"),
        )


class TestVariables(TestCase):
    def test_create_Variable(self):
        # Example usage:
        elements = {
            "COMPILE.mod": {
                "origin": "default",
                "private": False,
                "assign-recursive": "$(M2C) $(M2FLAGS) $(MODFLAGS) $(TARGET_ARCH)",
            },
            ".VARIABLES": {"origin": "default", "private": False, "assign": ""},
            "PWD": {
                "origin": "environment",
                "private": False,
                "assign-recursive": "/mnt/vols/t_home/home/tnmurphy/build/make-experimental",
            },
            "%D": {
                "origin": "automatic",
                "private": False,
                "assign-recursive": ".......",
            },
        }

        variables = {k: Variable.from_dict(k, v) for k, v in elements.items()}
        self.assertIsNotNone(variables)


@dataclass
class Commands:
    source: str
    commands: str

    @classmethod
    def from_dict(cls, d: dict):
        f = cls()
        for k in cls.__annotations__.keys():
            f.__setattr__(k, d[k])
        return f


@dataclass
class Rule:
    targets: List[str]
    terminal: Optional[bool]
    deps: List[str]
    ood_deps: List[str]
    cmds: Commands

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "Rule":
        """Factory function to create a Rule from a dictionary."""
        print(f"RULE: {data}")
        cmds = Commands(
            source=data["cmds"]["source"], commands=data["cmds"]["commands"]
        )

        args = {k: v for (k, v) in data.items() if k != "cmds"}
        return cls(**args, cmds=cmds)


class TestRule(TestCase):
    def test_create_Rule(self):
        # Example usage:
        rule_element = {
            "targets": ["%"],
            "terminal": True,
            "deps": ["s.%"],
            "ood-deps": [],
            "cmds": {
                "source": "builtin",
                "commands": "$(GET) $(GFLAGS) $(SCCS_OUTPUT_OPTION) $<",
            },
        }

        rule = Rule.from_dict(rule_element)
        self.assertIsNotNone(rule)


@dataclass
class File:
    hname: str
    vpath: str
    deps: list
    cmds: Commands
    stem: str
    also_make: list
    target_variables: dict
    last_mtime: int
    mtime_before_update: int
    considered: int
    command_flags: int
    update_status: int
    command_state: str
    builtin: bool
    precious: bool
    loaded: bool
    unloaded: bool
    low_resolution_time: bool
    tried_implicit: bool
    updating: bool
    updated: bool
    is_target: bool
    cmd_target: bool
    phony: bool
    intermediate: bool
    is_explicit: bool
    secondary: bool
    notintermediate: bool
    dontcare: bool
    ignore_vpath: bool
    pat_searched: bool
    no_diag: bool
    was_shuffled: bool
    snapped: bool

    @classmethod
    def from_dict(cls, d: dict):
        f = cls()
        for k in cls.__annotations__.keys():
            if k == "cmds":
                f.cmds = Commands.from_json(d[k])
            else:
                f.__setattr__(k, d[k])
        return f


@dataclass
class Directory:
    path: str
    status: str
    device: int = 0
    inode: int = 0
    files: int = 0
    impossibilities: int = 0

    @classmethod
    def from_dict(cls, path: str, d: dict):
        f = cls(path=path, **d)
        return f


@dataclass
class VPath:
    paths: list[str]
    vpaths: int
    nvpaths: int
    general_vpath: list[str]


@dataclass
class Stats:
    pass


@dataclass
class Makefile:
    global_variables: list[Variable]
    directories: list[Directory]
    implicit_rules: list[Rule]
    # files: list[File]
    # vpath: VPath
    # stats: Stats

    @classmethod
    def from_dict(cls, d: dict):
        global_variables = []
        pattern_specific_variables = []
        directories = []
        implicit_rules = []
        for makefile, data in j.items():
            for key, val in data.items():
                if key == "variables":
                    for vk, vv in val["global"].items():
                        global_variables.append(Variable.from_dict(vk, vv))
                elif key == "directories":
                    print("DIRS",val)
                    for path, dirdata in val.items():
                        if path == "":
                            continue
                        directories.append(Directory.from_dict(path, dirdata))
                    pass
                elif key == "rules":
                    for rv in val["implicit_rules"]:
                        r = Rule.from_dict(rv)
                        implicit_rules.append(r)
                elif key == "files":
                    pass
                elif key == "vpath":
                    pass
                elif key == "stats":
                    pass
            mf = cls(
                global_variables=global_variables,
                directories=directories,
                implicit_rules=implicit_rules,
            )
        return mf


if __name__ == "__main__":
    import json
    import sys

    filename = sys.argv[1]
    with open(filename, "r") as r:
        j = json.load(r)
        mf = Makefile.from_dict(j)
        print(mf)
