/**
 * ======================================================================================
 * 3D CLOTH SIMULATION (Verlet Integration)
 * ======================================================================================
 *
 * Concept:
 * This simulation uses a "Mass-Spring" model.
 * - MASS:   Represented by 'Points' (particles).
 * - SPRING: Represented by 'Links' (constraints keeping points at fixed distance).
 *
 * ASCII Visualization of the Grid:
 *
 * P ― Link ― P ― Link ― P
 * |          |          |
 * Link      Link       Link
 * |          |          |
 * P ― Link ― P ― Link ― P
 *
 * P = Point (Particle)
 * | = Vertical Link
 * ― = Horizontal Link
 *
 * ======================================================================================
 */

#include <SFML/Graphics.hpp>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>

// --- Configuration Constants ---
const int WIDTH = 70;        // Number of points horizontally
const int HEIGHT = 45;       // Number of points vertically
const float DISTANCE = 18.f; // Resting distance between points
const float GRAVITY = 0.35f; // Downward force per frame
const float AIR_FRICTION = 0.98f; // Velocity damping factor. Lower value = more drag.
const float STRETCH_LIMIT = 5.0f; // Multiplier for link breaking threshold
const float FOCAL_LENGTH = 900.f;
const float CAMERA_OFFSET_Z = 500.f;

const float BALL_RADIUS = 16.f;
const float BALL_AIR_FRICTION = 0.995f;
const float BALL_GRAVITY_SCALE = 0.45f;
const float BALL_MAX_PULL = 260.f;
const float BALL_FORCE_SCALE_XY = 0.38f;
const float BALL_FORCE_SCALE_Z = 0.95f;
const float HOLE_RADIUS = 45.f;
const float BALL_START_Z = -260.f;
const int MAX_BALLS = 20;
const float BALL_RESTITUTION_WALL = 0.72f;
const float BALL_RESTITUTION_FLOOR = 0.35f;
const float BALL_GROUND_FRICTION = 0.82f;
const float BALL_SLEEP_THRESHOLD = 0.30f;

/**
 * ------------------------------------------------------------------
 * STRUCT: Point
 * Represents a single particle in the cloth mesh.
 * ------------------------------------------------------------------
 *
 * VERLET INTEGRATION EXPLAINED:
 * Instead of storing velocity explicitly, we store the previous position.
 * Velocity is implicitly derived:
 *
 * PrevPos        CurrentPos        NextPos
 * O ―――――――――――> O ―――――――――――> O
 * ^                 ^
 * (Pos - Prev)      Apply this delta
 * is the vector     to current pos
 *
 * ------------------------------------------------------------------
 */
struct Point
{
    sf::Vector3f pos;       // Current Position (x, y, z)
    sf::Vector3f prevPos;   // Position in the previous frame
    bool locked = false;    // If true, the point is pinned (static)
    bool isGrabbed = false; // If true, currently held by mouse

    Point(float x, float y, float z) : pos(x, y, z), prevPos(x, y, z) {}

    void update(float time)
    {
        if (locked || isGrabbed)
            return;

        // 1. Calculate Velocity (Verlet)
        sf::Vector3f vel = (pos - prevPos) * AIR_FRICTION;

        // 2. Update Positions
        prevPos = pos;
        pos += vel;
        pos.y += GRAVITY; // Apply gravity force

        // 3. Simulate Wind (Sine wave on Z-axis)
        //    Adds a subtle oscillation to make it look alive.
        pos.z += std::sin(time + pos.x * 0.05f) * 0.15f;
        pos.z *= 0.99f; // Damping on Z to prevent infinite oscillation
    }
};

/**
 * ------------------------------------------------------------------
 * STRUCT: Link
 * Represents the constraint (stick) between two points.
 * ------------------------------------------------------------------
 *
 * CONSTRAINT SOLVING:
 * We want the distance (d) between P1 and P2 to always equal targetDist.
 * If (d != targetDist), we push/pull P1 and P2 to fix it.
 *
 * P1 <---- (correction) ----> P2
 *
 * ------------------------------------------------------------------
 */
struct Link
{
    Point *p1;
    Point *p2;
    float targetDist;    // The resting length of the link
    bool broken = false; // True if the link has been cut or snapped

    Link(Point &a, Point &b) : p1(&a), p2(&b)
    {
        sf::Vector3f d = p1->pos - p2->pos;
        targetDist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    }

    void solve()
    {
        if (broken)
            return;

        sf::Vector3f diff = p1->pos - p2->pos;
        float dist = std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);

        // --- TEAR LOGIC ---
        // If stretched too far (5x length), the link snaps.
        if (dist > targetDist * STRETCH_LIMIT)
        {
            broken = true;
            return;
        }

        // Avoid division by zero
        if (dist < 0.1f)
            return;

        // Calculate the correction factor
        // (Difference between current dist and target dist)
        float factor = (targetDist - dist) / dist * 0.5f; // 0.5 because each point moves half the error
        sf::Vector3f offset = diff * factor;

        // Apply correction if points are not locked/grabbed
        if (!p1->locked && !p1->isGrabbed)
            p1->pos += offset;
        if (!p2->locked && !p2->isGrabbed)
            p2->pos -= offset;
    }
};

struct Ball
{
    sf::Vector3f pos;
    sf::Vector3f prevPos;
    float radius = BALL_RADIUS;
    bool active = true;

    Ball(const sf::Vector3f &startPos, const sf::Vector3f &initialVelocity, float r = BALL_RADIUS)
        : pos(startPos), prevPos(startPos - initialVelocity), radius(r) {}

    void update()
    {
        if (!active)
            return;

        sf::Vector3f vel = (pos - prevPos) * BALL_AIR_FRICTION;
        prevPos = pos;
        pos += vel;
        pos.y += GRAVITY * BALL_GRAVITY_SCALE;
    }
};

/**
 * ------------------------------------------------------------------
 * FUNCTION: Project
 * Converts 3D World Coordinates (x,y,z) to 2D Screen Coordinates (x,y).
 * ------------------------------------------------------------------
 *
 * Eye/Camera
 * O
 * \
 * \   Screen Plane
 * \       |
 * \      |
 * \     v  (Projected Point)
 * \____.
 * \   |
 * \  |
 * \ |
 * \|
 * O (Actual 3D Point)
 *
 * Formula: screen_x = x * (focalLength / (focalLength + z))
 * ------------------------------------------------------------------
 */
sf::Vector2f project(sf::Vector3f p, sf::Vector2u winSize)
{
    // Perspective division: Things further away (high Z) get smaller.
    // +500.f is the camera offset (distance from the cloth).
    float perspective = FOCAL_LENGTH / (FOCAL_LENGTH + p.z + CAMERA_OFFSET_Z);

    return {
        winSize.x / 2.f + p.x * perspective, // Center X
        winSize.y / 10.f + p.y * perspective // Offset Y slightly
    };
}

sf::Vector3f unprojectToWorld(sf::Vector2f p, float worldZ, sf::Vector2u winSize)
{
    float perspective = FOCAL_LENGTH / (FOCAL_LENGTH + worldZ + CAMERA_OFFSET_Z);
    return {
        (p.x - winSize.x / 2.f) / perspective,
        (p.y - winSize.y / 10.f) / perspective,
        worldZ};
}

float distance3D(const sf::Vector3f &a, const sf::Vector3f &b)
{
    const sf::Vector3f d = a - b;
    return std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
}

float dot3(const sf::Vector3f &a, const sf::Vector3f &b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

sf::Vector3f normalizedOr(const sf::Vector3f &v, const sf::Vector3f &fallback)
{
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1e-5f)
        return fallback;
    return v / len;
}

float distancePointSegment3D(const sf::Vector3f &point, const sf::Vector3f &a, const sf::Vector3f &b, sf::Vector3f *closestPoint = nullptr)
{
    const sf::Vector3f ab = b - a;
    const sf::Vector3f ap = point - a;
    const float abLenSq = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z;
    if (abLenSq < 1e-6f)
    {
        if (closestPoint)
            *closestPoint = a;
        return distance3D(point, a);
    }

    float t = (ap.x * ab.x + ap.y * ab.y + ap.z * ab.z) / abLenSq;
    t = std::max(0.f, std::min(1.f, t));
    const sf::Vector3f closest = a + ab * t;

    if (closestPoint)
        *closestPoint = closest;

    return distance3D(point, closest);
}

/**
 * ------------------------------------------------------------------
 * FUNCTION: Intersects
 * Checks if two 2D line segments intersect. Used for "cutting" links.
 * ------------------------------------------------------------------
 */
bool intersects(sf::Vector2f a, sf::Vector2f b, sf::Vector2f c, sf::Vector2f d)
{
    // CCW (Counter-Clockwise) helper function
    auto ccw = [](sf::Vector2f p0, sf::Vector2f p1, sf::Vector2f p2)
    {
        return (p2.y - p0.y) * (p1.x - p0.x) > (p1.y - p0.y) * (p2.x - p0.x);
    };
    return ccw(a, c, d) != ccw(b, c, d) && ccw(a, b, c) != ccw(a, b, d);
}

// ======================================================================================
// MAIN FUNCTION
// ======================================================================================
int main()
{
    // 1. Setup Window
    sf::RenderWindow window(sf::VideoMode({1400u, 900u}), "SFML 3D Cloth Simulation");
    window.setFramerateLimit(60);

    sf::Clock clock;
    std::vector<Point> points;
    std::vector<Link> links;
    std::vector<Ball> balls;

    // Interaction State
    Point *grabbedPoint = nullptr;
    sf::Vector2f lastMousePos;
    bool chargingShot = false;
    sf::Vector2f shotAnchor;

    // 2. Initialize Points (Grid)
    //    Loops Y then X to create the mesh
    for (int y = 0; y < HEIGHT; y++)
    {
        for (int x = 0; x < WIDTH; x++)
        {
            // Center the cloth horizontally
            points.emplace_back(x * DISTANCE - (WIDTH * DISTANCE) / 2.f, y * DISTANCE, 0.f);

            // Pin the top row so the cloth hangs
            if (y == 0)
                points.back().locked = true;
        }
    }

    // 3. Initialize Links (Connections)
    //    Connects right (x+1) and down (y+1)
    for (int y = 0; y < HEIGHT; y++)
    {
        for (int x = 0; x < WIDTH; x++)
        {
            if (x < WIDTH - 1) // Link to Right
                links.emplace_back(points[y * WIDTH + x], points[y * WIDTH + x + 1]);

            if (y < HEIGHT - 1) // Link Down
                links.emplace_back(points[y * WIDTH + x], points[(y + 1) * WIDTH + x]);
        }
    }

    // 4. Main Game Loop
    while (window.isOpen())
    {
        float elapsed = clock.getElapsedTime().asSeconds();
        sf::Vector2u winSize = window.getSize();
        const sf::Vector2i mousePosI = sf::Mouse::getPosition(window);
        sf::Vector2f mPos(static_cast<float>(mousePosI.x), static_cast<float>(mousePosI.y));

        // --- Event Polling ---
        while (const std::optional<sf::Event> event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
                window.close();

            // Handle Mouse Click (Grabbing)
            if (const auto *mousePressed = event->getIf<sf::Event::MouseButtonPressed>())
            {
                if (mousePressed->button == sf::Mouse::Button::Left)
                {
                    // Find the nearest point to the mouse cursor
                    float minDist = 50.f; // interaction radius
                    for (auto &p : points)
                    {
                        sf::Vector2f proj = project(p.pos, winSize);
                        float dx = proj.x - mPos.x;
                        float dy = proj.y - mPos.y;
                        float d = std::sqrt(dx * dx + dy * dy);

                        if (d < minDist && !p.locked)
                        {
                            minDist = d;
                            grabbedPoint = &p;
                        }
                    }
                    if (grabbedPoint)
                        grabbedPoint->isGrabbed = true;
                }
                else if (mousePressed->button == sf::Mouse::Button::Middle && !grabbedPoint)
                {
                    chargingShot = true;
                    shotAnchor = mPos;
                }
            }

            // Handle Mouse Release
            if (const auto *mouseReleased = event->getIf<sf::Event::MouseButtonReleased>())
            {
                if (mouseReleased->button == sf::Mouse::Button::Left)
                {
                    if (grabbedPoint)
                    {
                        grabbedPoint->isGrabbed = false;
                        grabbedPoint = nullptr;
                    }
                }
                else if (mouseReleased->button == sf::Mouse::Button::Middle && chargingShot)
                {
                    const sf::Vector2f pull = shotAnchor - mPos;
                    const float pullLen = std::sqrt(pull.x * pull.x + pull.y * pull.y);
                    if (pullLen > 3.f)
                    {
                        const float clampedLen = std::min(BALL_MAX_PULL, pullLen);
                        const sf::Vector2f pullDir = pull / pullLen;
                        const sf::Vector2f clampedPull = pullDir * clampedLen;

                        const sf::Vector3f startPos = unprojectToWorld(shotAnchor, BALL_START_Z, winSize);
                        const sf::Vector3f velocity = {
                            clampedPull.x * BALL_FORCE_SCALE_XY,
                            clampedPull.y * BALL_FORCE_SCALE_XY,
                            clampedLen * BALL_FORCE_SCALE_Z + 12.f};

                        balls.emplace_back(startPos, velocity);
                        if (balls.size() > MAX_BALLS)
                            balls.erase(balls.begin());
                    }

                    chargingShot = false;
                }
            }
        }

        // --- Logic: Dragging Points ---
        if (grabbedPoint)
        {
            // Reverse projection to move 3D point with 2D mouse
            float focalLength = 900.f;
            float perspective = focalLength / (focalLength + grabbedPoint->pos.z + 500.f);

            grabbedPoint->pos.x = (mPos.x - winSize.x / 2.f) / perspective;
            grabbedPoint->pos.y = (mPos.y - winSize.y / 10.f) / perspective;

            // Reset velocity when dragging (prevent slingshot effect)
            grabbedPoint->prevPos = grabbedPoint->pos;
        }

        // --- Logic: Cutting Links (Right Click) ---
        if (sf::Mouse::isButtonPressed(sf::Mouse::Button::Right))
        {
            for (auto &l : links)
            {
                sf::Vector2f p1 = project(l.p1->pos, winSize);
                sf::Vector2f p2 = project(l.p2->pos, winSize);

                // If the mouse trail intersects the link line, break it
                if (intersects(lastMousePos, mPos, p1, p2))
                {
                    l.broken = true;
                }
            }
        }

        // --- Logic: Ball Physics + Cloth Impact ---
        for (auto &ball : balls)
        {
            if (!ball.active)
                continue;

            ball.update();

            sf::Vector3f impactPoint;
            bool collided = false;
            for (auto &l : links)
            {
                if (l.broken)
                    continue;

                const float hitDist = distancePointSegment3D(ball.pos, l.p1->pos, l.p2->pos, &impactPoint);
                if (hitDist <= ball.radius)
                {
                    collided = true;
                    l.broken = true;
                    break;
                }
            }

            if (collided)
            {
                for (auto &l : links)
                {
                    if (l.broken)
                        continue;
                    if (distance3D(l.p1->pos, impactPoint) < HOLE_RADIUS || distance3D(l.p2->pos, impactPoint) < HOLE_RADIUS)
                        l.broken = true;
                }

                // Bounce from cloth instead of disappearing.
                const sf::Vector3f vel = ball.pos - ball.prevPos;
                const sf::Vector3f normal = normalizedOr(ball.pos - impactPoint, sf::Vector3f(0.f, 0.f, -1.f));
                const float vDotN = dot3(vel, normal);
                sf::Vector3f reflected = vel;
                if (vDotN < 0.f)
                    reflected = vel - normal * (2.f * vDotN);

                reflected *= 0.55f;
                ball.pos = impactPoint + normal * (ball.radius + 0.5f);
                ball.prevPos = ball.pos - reflected;
            }

            // Room boundaries: side walls + front/back walls + floor.
            const float roomHalfX = WIDTH * DISTANCE * 0.85f;
            const float roomNearZ = -420.f;
            const float roomFarZ = 1200.f;
            const float floorY = HEIGHT * DISTANCE * 2.2f;

            sf::Vector3f vel = ball.pos - ball.prevPos;

            if (ball.pos.x - ball.radius < -roomHalfX)
            {
                ball.pos.x = -roomHalfX + ball.radius;
                vel.x = -vel.x * BALL_RESTITUTION_WALL;
            }
            else if (ball.pos.x + ball.radius > roomHalfX)
            {
                ball.pos.x = roomHalfX - ball.radius;
                vel.x = -vel.x * BALL_RESTITUTION_WALL;
            }

            if (ball.pos.z - ball.radius < roomNearZ)
            {
                ball.pos.z = roomNearZ + ball.radius;
                vel.z = -vel.z * BALL_RESTITUTION_WALL;
            }
            else if (ball.pos.z + ball.radius > roomFarZ)
            {
                ball.pos.z = roomFarZ - ball.radius;
                vel.z = -vel.z * BALL_RESTITUTION_WALL;
            }

            if (ball.pos.y + ball.radius > floorY)
            {
                ball.pos.y = floorY - ball.radius;
                vel.y = -vel.y * BALL_RESTITUTION_FLOOR;
                vel.x *= BALL_GROUND_FRICTION;
                vel.z *= BALL_GROUND_FRICTION;

                // Let the ball settle on the floor instead of jittering forever.
                if (std::fabs(vel.y) < BALL_SLEEP_THRESHOLD)
                {
                    vel.y = 0.f;
                    if (std::fabs(vel.x) < BALL_SLEEP_THRESHOLD)
                        vel.x = 0.f;
                    if (std::fabs(vel.z) < BALL_SLEEP_THRESHOLD)
                        vel.z = 0.f;
                }
            }

            ball.prevPos = ball.pos - vel;
        }

        // --- Logic: Physics Solver ---
        // Iterate multiple times per frame for stability (stiffer cloth)
        // 1 iteration = rubbery/stretchy
        // 8 iterations = rigid cloth
        for (int i = 0; i < 8; i++)
        {
            for (auto &l : links)
                l.solve();
        }

        // Remove broken links from the vector efficiently
        links.erase(std::remove_if(links.begin(), links.end(),
                                   [](const Link &l)
                                   { return l.broken; }),
                    links.end());
        balls.erase(std::remove_if(balls.begin(), balls.end(),
                                   [](const Ball &b)
                                   { return !b.active; }),
                    balls.end());

        // Update individual point physics (gravity, wind)
        for (auto &p : points)
            p.update(elapsed * 1.5f);

        lastMousePos = mPos;

        // --- Rendering ---
        window.clear(sf::Color(10, 10, 15)); // Dark Blue/Grey background

        // Use VertexArray for high performance rendering of many lines
        sf::VertexArray va(sf::PrimitiveType::Lines);
        for (const auto &l : links)
        {
            sf::Vector2f v1 = project(l.p1->pos, winSize);
            sf::Vector2f v2 = project(l.p2->pos, winSize);

            // Depth Shading:
            // Calculate color based on Z-depth (closer = brighter, further = darker)
            float depth = std::max(0.f, std::min(1.f, (l.p1->pos.z + 100.f) / 400.f));
            std::uint8_t colorVal = static_cast<std::uint8_t>(255 * (1.0f - depth));

            // Set color (Yellow if grabbed, Blue-ish otherwise)
            sf::Color col = l.p1->isGrabbed ? sf::Color::Yellow : sf::Color(50, colorVal, 255);

            va.append(sf::Vertex{v1, col});
            va.append(sf::Vertex{v2, col});
        }

        window.draw(va);

        // Render all active balls
        for (const auto &ball : balls)
        {
            const sf::Vector2f screenPos = project(ball.pos, winSize);
            const float perspective = FOCAL_LENGTH / (FOCAL_LENGTH + ball.pos.z + CAMERA_OFFSET_Z);
            const float radius2D = std::max(3.f, ball.radius * perspective);

            sf::CircleShape circle(radius2D);
            circle.setOrigin({radius2D, radius2D});
            circle.setPosition(screenPos);
            circle.setFillColor(sf::Color(255, 120, 80, 210));
            circle.setOutlineThickness(1.5f);
            circle.setOutlineColor(sf::Color(255, 225, 170));
            window.draw(circle);
        }

        // Render launch guide when holding middle mouse button
        if (chargingShot)
        {
            sf::Vector2f pull = shotAnchor - mPos;
            const float pullLen = std::sqrt(pull.x * pull.x + pull.y * pull.y);
            if (pullLen > BALL_MAX_PULL)
                pull = pull / pullLen * BALL_MAX_PULL;

            sf::VertexArray aim(sf::PrimitiveType::Lines);
            aim.append(sf::Vertex{shotAnchor, sf::Color(255, 220, 120)});
            aim.append(sf::Vertex{shotAnchor - pull, sf::Color(255, 120, 80)});
            window.draw(aim);

            sf::CircleShape anchor(6.f);
            anchor.setOrigin({6.f, 6.f});
            anchor.setPosition(shotAnchor);
            anchor.setFillColor(sf::Color(255, 235, 170));
            window.draw(anchor);
        }

        window.display();
    }

    return 0;
}