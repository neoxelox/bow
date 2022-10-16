from matplotlib import animation, pyplot
from superinvoke import task

from .plotter import Plotter
from .tools import Tools


@task()
def lint(context):
    """Lint source."""
    context.run(f"{Tools.Linter} --enable=all -i build .")


@task()
def plot(context, port="/dev/ttyACM0", baud=115200, length=100, match="\{(.*?)\}", rate=10):
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
