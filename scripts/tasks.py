from matplotlib import animation, pyplot
from multiprocessing import cpu_count
from superinvoke import task

from .plotter import Plotter
from .tools import Tools


@task()
def lint(context):
    """Lint source."""
    context.print("Linting (this may take a while)...")
    context.run(
        f"{Tools.Linter} "
        f"--quiet -j{cpu_count()} "
        "--project=build/compile_commands.json --cppcheck-build-dir=build "
        "--suppress='*:*esp-idf*' --suppress='*:*vendor*' "
        "--suppress='missingIncludeSystem' --suppress='unusedFunction' --suppress='unusedPrivateFunction' "
        "--std=c++20 --enable=all --check-level=exhaustive"
    )


@task()
def plot(
    context, port="/dev/ttyACM0", baud=115200, length=100, match="\{(.*?)\}", rate=10
):
    """Launch serial plotter."""
    figure = pyplot.figure()
    axes = pyplot.axes(xlim=(0, length), ylim=(0, 1 * 1.1))

    figure.canvas.manager.set_window_title("Serial Plotter")
    axes.set_title("Serial Plotter")
    axes.set_xlabel("Time")
    axes.set_ylabel("Values")

    plotter = Plotter(port, baud, length, match, axes)

    # Animation must be assigned to a variable
    _ = animation.FuncAnimation(figure, plotter.animate, interval=rate, blit=True)

    try:
        plotter.run()
        pyplot.legend(loc="upper left")
        pyplot.show()
    finally:
        plotter.close()


@task()
def erase(context, yes=False):
    "Erase the entire flash contents."

    context.print(f"The entire flash contents will be [bold red1]erased[/bold red1]")

    if not yes and context.input("Continue? y/N: ").lower() != "y":
        context.exit()

    context.run(f"{Tools.Idf} erase-flash")
