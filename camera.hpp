#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

unsigned int k_complement_control_command = 0xFFFFFFFF;

enum class GameCommand : unsigned int
{
	forward  = 1 << 0,                 // W
	backward = 1 << 1,                 // S
	left     = 1 << 2,                 // A
	right    = 1 << 3,                 // D
	invalid  = (unsigned int)(1 << 31) // lost focus
};

class Camera
{
public:
	Camera()
	{
	}

	void init(int viewportWidth, int viewportHeight)
	{
		m_viewportWidth = viewportWidth;
		m_viewportHeight = viewportHeight;
	}

	void setCommand(unsigned int command) { m_command = command; }

	void update(const float deltaTime)
	{
        float speed = 2.0f;
        glm::vec3 moveDirection(0.f, 0.f, 0.f);
        bool hasMoveCommand = false;
        if ((unsigned int)GameCommand::forward & m_command)
        {
            moveDirection += m_forward;
            hasMoveCommand = true;
        }
        if ((unsigned int)GameCommand::backward & m_command)
        {
            moveDirection -= m_forward;
            hasMoveCommand = true;
        }
        if (hasMoveCommand)
        {
            m_pos += moveDirection * deltaTime * speed;
            m_lookAt = m_pos + m_forward;
        }
	}

	glm::mat4 view() const
	{
		return glm::lookAt(m_pos, m_lookAt, m_up);
	}

	glm::mat4 project() const
	{
		float aspect = static_cast<float>(m_viewportWidth) / static_cast<float>(m_viewportHeight);
		glm::mat4 proj = glm::perspective(m_fovy, aspect, m_zNear, m_zFar);
		proj[1][1] *= -1; // glm是为opengl设计，其中裁剪空间的Y是倒置的，补偿方法是把投影矩阵的y轴缩放翻转
		return proj;
	}

private:
	glm::vec3 m_pos = glm::vec3(2.0f, 2.0f, 2.0f);
	glm::vec3 m_lookAt = glm::vec3(0.0f, 0.0f, 0.0f);
	glm::vec3 m_up = glm::vec3(0.0f, 0.0f, 1.0f);
	float m_fovy = glm::radians(45.0f);
	float m_zNear = 0.1f;
	float m_zFar = 10.0f;
	int m_viewportWidth;
	int m_viewportHeight;

    glm::vec3 m_forward = glm::vec3(-2.0f, -2.0f, -2.0f);

	unsigned int m_command;
};
