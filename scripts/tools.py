import superinvoke

from .constants import Tags


class Tools(superinvoke.Tools):
    Git = superinvoke.Tool(
        name="git",
        version="2.34.1",
        tags=[*Tags.As("*")],
        path="git",
    )

    Linter = superinvoke.Tool(
        name="cppcheck",
        version="2.7",
        tags=[Tags.DEV, Tags.CI],
        path="cppcheck",
    )

    Idf = superinvoke.Tool(
        name="esp-idf",
        version="5.0-beta1-641-gc321739074",
        tags=[*Tags.As("*")],
        path="idf.py",
    )
