import superinvoke

from .constants import Tags


class Tools(superinvoke.Tools):
    Git = superinvoke.Tool(
        name="git",
        version=">=2.39.3",
        tags=[*Tags.As("*")],
        path="git",
    )

    Python = superinvoke.Tool(
        name="python",
        version=">=3.11.2",
        tags=[Tags.DEV, Tags.CI],
        path="python",
    )

    Pip = superinvoke.Tool(
        name="pip",
        version=">=23.1.2",
        tags=[Tags.DEV, Tags.CI],
        path="pip",
    )

    Linter = superinvoke.Tool(
        name="cppcheck",
        version=">=2.12.1",
        tags=[Tags.DEV, Tags.CI],
        path="cppcheck",
    )

    Idf = superinvoke.Tool(
        name="esp-idf",
        version="5.1",
        tags=[*Tags.As("*")],
        path="idf.py",
    )
