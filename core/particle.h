#ifndef VSB_SEMESTRAL_PROJECT_PARTICLE_H
#define VSB_SEMESTRAL_PROJECT_PARTICLE_H

#include <glm/glm.hpp>
#include <ostream>

struct Particle {
public:
    union {
        struct {
            float tx, ty, tz;
            float rx, ry, rz;
        };
        float data[6];
    };

    // Personal best
    union {
        struct {
            float tx, ty, tz;
            float rx, ry, rz;
        };
        float data[6];
    } pBest;

    float fitness = 0;

    Particle() = default;
    Particle(float tx, float ty, float tz, float rx, float ry, float rz);

    glm::mat4 model();

    friend std::ostream &operator<<(std::ostream &os, const Particle &particle);
};


#endif //VSB_SEMESTRAL_PROJECT_PARTICLE_H
