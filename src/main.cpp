// Copyright (c) 2018 Martyn Afford
// Licensed under the MIT licence

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <ncursesw/cursesw.h>
#include <random>

namespace {

// RAII wrapper around ncurses.
struct ncurses {
    ncurses()
    {
        setlocale(LC_ALL, "");
        initscr();
        cbreak();
        nonl();
        noecho();
        nodelay(stdscr, true);
        curs_set(0);
    }

    ~ncurses()
    {
        endwin();
    }
};

// Encapsulate a two-dimensional grid.
class grid {
public:
    const int width;
    const int height;

    grid(int w, int h)
        : width{w}
        , height{h}
        , data{new bool[width * height]}
    {}

    bool&
    operator()(int x, int y)
    {
        assert(0 <= x && x < width && 0 <= y && y < height);
        return data[x + y * width];
    }

    const bool&
    operator()(int x, int y) const
    {
        assert(0 <= x && x < width && 0 <= y && y < height);
        return data[x + y * width];
    }

private:
    std::unique_ptr<bool[]> data;
};

// Encapsulate two grids to provide double-buffering, where the grids alternate
// being the main (front) buffer or the secondary (back).
class double_buffered_grid {
public:
    double_buffered_grid(int width, int height)
        : a{width, height}
        , b{width, height}
    {}

    const grid&
    front() const
    {
        return first ? a : b;
    }

    grid&
    back()
    {
        return first ? b : a;
    }

    void
    swap()
    {
        first = !first;
    }

private:
    bool first = true;
    grid a;
    grid b;
};

// Given two grids (of equal size) produce the next generation according to the
// rules of Conway's game of life. The rules are as follows, quoting Wikipedia:
//
// - Any live cell with fewer than two live neighbors dies, as if by
//   underpopulation.
// - Any live cell with two or three live neighbors lives on to the next
//   generation.
// - Any live cell with more than three live neighbors dies, as if by
//   overpopulation.
// - Any dead cell with exactly three live neighbors becomes a live cell, as if
//   by reproduction.
void
next_generation(const grid& in, grid& out)
{
    assert(in.width == out.width && in.height == out.height);

    for (auto y = 0; y != in.height; ++y) {
        for (auto x = 0; x != in.width; ++x) {
            auto left = x != 0;
            auto up = y != 0;
            auto right = x != in.width - 1;
            auto down = y != in.height - 1;

            auto currently_alive = in(x, y);

            // This code causes strict-overflow warnings that can be safely
            // ignored.
            auto neighbours = (left && up && in(x - 1, y - 1) ? 1 : 0) +
                              (left && in(x - 1, y) ? 1 : 0) +
                              (left && down && in(x - 1, y + 1) ? 1 : 0) +
                              (up && in(x, y - 1) ? 1 : 0) +
                              (down && in(x, y + 1) ? 1 : 0) +
                              (right && up && in(x + 1, y - 1) ? 1 : 0) +
                              (right && in(x + 1, y) ? 1 : 0) +
                              (right && down && in(x + 1, y + 1) ? 1 : 0);

            if (currently_alive) {
                out(x, y) = neighbours == 2 || neighbours == 3;
            } else {
                out(x, y) = neighbours == 3;
            }
        }
    }
}

// Represent the game of life, including rendering. The constructor generates a
// random board to start.
class game_of_life {
public:
    game_of_life(int width, int height)
        : buffer{width, height}
    {
        auto& grid = buffer.back();
        auto seed = std::chrono::system_clock::now().time_since_epoch().count();
        auto frequency = 3; // lower means more initial alive cells

        std::default_random_engine engine{seed};
        std::uniform_int_distribution<int> distribution{0, frequency};

        for (auto i = 0; i != width * height; ++i) {
            grid(i % width, i / width) = distribution(engine) == 0;
        }

        buffer.swap();
    }

    void
    render() const
    {
        assert(buffer.front().height % 2 == 0);

        auto full = "█";
        auto upper = "▀";
        auto lower = "▄";

        auto& grid = buffer.front();

        for (auto y = 0; y != grid.height; y += 2) {
            move(y / 2, 0);

            for (auto x = 0; x != grid.width; ++x) {
                auto top = grid(x, y);
                auto bottom = grid(x, y + 1);

                addstr(top ? (bottom ? full : upper) : (bottom ? lower : " "));
            }
        }

        refresh();
    }

    void
    tick()
    {
        next_generation(buffer.front(), buffer.back());
        buffer.swap();
    }

private:
    double_buffered_grid buffer;
};

} // namespace

int
main()
{
    constexpr auto min_tick_ms = 16;
    constexpr auto max_tick_ms = 1024;

    auto width = 0;
    auto height = 0;
    auto tick_ms = 32;
    auto running = true;
    ncurses nc;

    getmaxyx(stdscr, height, width);
    std::unique_ptr<game_of_life> game{new game_of_life{width, height * 2}};

    timeout(tick_ms);
    game->render();

    while (true) {
        auto key = getch();

        // If we resized, generate a new board with the appropriate size
        if (key == KEY_RESIZE) {
            getmaxyx(stdscr, height, width);
            game.reset(new game_of_life{width, height * 2});
            game->render();
            continue;
        }

        // Decrease speed
        if (key == '-' && tick_ms < max_tick_ms) {
            tick_ms *= 2;
            timeout(tick_ms);
        }

        // Increase speed
        if (key == '+' && tick_ms > min_tick_ms) {
            tick_ms /= 2;
            timeout(tick_ms);
        }

        // Play/pause
        if (key == 'p' || key == ' ') {
            running = !running;
        }

        // If the game is paused you can still step through generations
        if (running || key == 's') {
            game->tick();
            game->render();
        }

        // Quit
        if (key == 'q') {
            break;
        }
    }

    return EXIT_SUCCESS;
}
