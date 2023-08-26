#include "StdInclude.hpp"
#include "CameraManager.hpp"

#include "../Events.hpp"
#include "../Input.hpp"
#include "Mod.hpp"
#include "Utilities/MathUtils.hpp"

namespace IWXMVM::Components
{
	std::string_view CameraManager::GetCameraModeLabel(Camera::Mode cameraMode)
	{
		switch (cameraMode)
		{
		case Camera::Mode::FirstPerson:
			return "First Person Camera";
		case Camera::Mode::ThirdPerson:
			return "Third Person Camera";
		case Camera::Mode::Free:
			return "Free Camera";
		case Camera::Mode::Orbit:
			return "Orbit Camera";
		case Camera::Mode::Dolly:
			return "Dolly Camera";
		case Camera::Mode::Bone:
			return "Bone Camera";
		default:
			return "Unknown Camera Mode";
		}
	}

	std::vector<Camera::Mode> CameraManager::GetCameraModes()
	{
		std::vector<Camera::Mode> cameraModes;
		for (int i = 0; i < (int)Camera::Mode::Count; i++)
		{
			cameraModes.push_back((Camera::Mode)i);
		}
		return cameraModes;
	}

	void CameraManager::UpdateFreecamMovement()
	{
		//if (!Input::GetFocusedWindow() == GameView)
		//	return;
	
		auto& activeCamera = GetActiveCamera();
		auto& cameraPosition = activeCamera.GetPosition();
	
		// TODO: make this configurable
		constexpr float FREECAM_SPEED = 200;
		constexpr float MOUSE_SPEED = 0.1f;
		constexpr float HEIGHT_CEILING = 250.0f;
		constexpr float HEIGHT_MULTIPLIER = 0.75f;
	
		const auto speedModifier = Input::KeyHeld(ImGuiKey_LeftShift) ? 1.5f : 1.0f;

		const auto cameraHeightSpeed = Input::GetDeltaTime() * FREECAM_SPEED;
		const auto cameraMovementSpeed = cameraHeightSpeed + HEIGHT_MULTIPLIER * (std::abs(cameraPosition[2]) / HEIGHT_CEILING) * speedModifier;
	
		if (Input::KeyHeld(ImGuiKey_W))
		{
			cameraPosition += activeCamera.GetForwardVector() * cameraMovementSpeed;
		}
	
		if (Input::KeyHeld(ImGuiKey_S))
		{
			cameraPosition -= activeCamera.GetForwardVector() * cameraMovementSpeed;
		}
	
		if (Input::KeyHeld(ImGuiKey_A))
		{
			cameraPosition += activeCamera.GetRightVector() * cameraMovementSpeed;
		}
	
		if (Input::KeyHeld(ImGuiKey_D))
		{
			cameraPosition -= activeCamera.GetRightVector() * cameraMovementSpeed;
		}
	
		activeCamera.GetRotation()[0] += Input::GetMouseDelta()[1] * MOUSE_SPEED;
		activeCamera.GetRotation()[1] -= Input::GetMouseDelta()[0] * MOUSE_SPEED;
	
		if (Input::KeyHeld(ImGuiKey_Space))
			cameraPosition[2] += cameraHeightSpeed;
	
		if (Input::KeyHeld(ImGuiKey_LeftAlt))
			cameraPosition[2] -= cameraHeightSpeed;
	}

	void CameraManager::UpdateOrbitCameraMovement()
	{
		auto& activeCamera = GetActiveCamera();
		auto& cameraPosition = activeCamera.GetPosition();
	
		constexpr float BASE_SPEED = 0.1f;
		constexpr float ROTATION_SPEED = BASE_SPEED * 2.0f;
		constexpr float TRANSLATION_SPEED = BASE_SPEED * 3.0f;
		constexpr float ZOOM_SPEED = BASE_SPEED * 8.0f;
		constexpr float HEIGHT_CEILING = 250.0f;
		constexpr float HEIGHT_MULTIPLIER = 1.5f;
		constexpr float SCROLL_LOWER_BOUNDARY = -0.001f;
		constexpr float SCROLL_UPPER_BOUNDARY = 0.001f;
		constexpr float MIN_ORBIT_DIST = 10;

		static double scrollDelta = 0.0;
		scrollDelta -= Input::GetScrollDelta() * ZOOM_SPEED;

		// bump camera out of origin if it's at the origin
		if (cameraPosition == orbitCameraOrigin)
		{
			cameraPosition = orbitCameraOrigin + glm::vector3::one;
		}

		if (Input::KeyDown(ImGuiKey_F4))
		{
			scrollDelta = 0.0;

			orbitCameraOrigin = glm::vector3::zero;
			cameraPosition = glm::vector3::one;
		}

		if (Input::MouseButtonHeld(ImGuiMouseButton_Middle))
		{
			auto horizontalDelta = -Input::GetMouseDelta()[0] * ROTATION_SPEED;
			cameraPosition -= orbitCameraOrigin;
			cameraPosition = glm::rotateZ(cameraPosition, MathUtils::DegreesToRadians(horizontalDelta));
			cameraPosition += orbitCameraOrigin;
			
			auto verticalDelta = Input::GetMouseDelta()[1] * ROTATION_SPEED;
			cameraPosition -= orbitCameraOrigin;
			cameraPosition = glm::rotate(cameraPosition, MathUtils::DegreesToRadians(verticalDelta), glm::cross(glm::vector3::up, activeCamera.GetForwardVector()));
			cameraPosition += orbitCameraOrigin;
		}

		if (Input::MouseButtonHeld(ImGuiMouseButton_Right))
		{
			// use the height value to move faster around at higher altitude 
			const float translationSpeed = TRANSLATION_SPEED + HEIGHT_MULTIPLIER * (std::abs(cameraPosition[2]) / HEIGHT_CEILING) * TRANSLATION_SPEED;

			glm::vec3 forward2D = glm::normalize(activeCamera.GetForwardVector());
			forward2D.z = 0;
			orbitCameraOrigin += forward2D * Input::GetMouseDelta()[1] * translationSpeed;
			cameraPosition += forward2D * Input::GetMouseDelta()[1] * translationSpeed;

			glm::vec3 right2D = glm::normalize(activeCamera.GetRightVector());
			right2D.z = 0;
			orbitCameraOrigin += right2D * Input::GetMouseDelta()[0] * translationSpeed;
			cameraPosition += right2D * Input::GetMouseDelta()[0] * translationSpeed;
		}

		activeCamera.SetForwardVector(orbitCameraOrigin - cameraPosition);
		
		if (scrollDelta < SCROLL_LOWER_BOUNDARY || scrollDelta > SCROLL_UPPER_BOUNDARY)
		{
			auto desiredPosition = cameraPosition + glm::normalize(cameraPosition - orbitCameraOrigin) * (0.025 * scrollDelta) * 100;
			if (glm::distance(desiredPosition, orbitCameraOrigin) > MIN_ORBIT_DIST)
			{
				cameraPosition = desiredPosition;
			}
			else 
			{
				cameraPosition = orbitCameraOrigin + glm::normalize(cameraPosition - orbitCameraOrigin) * MIN_ORBIT_DIST;
			}
			scrollDelta *= 0.975;
		} 
		else if (scrollDelta != 0.0) 
		{
			scrollDelta = 0.0;
		}
	}

	void CameraManager::UpdateCameraFrame()
	{
		if (Mod::GetGameInterface()->GetGameState() == Types::GameState::MainMenu) {
			return;
		}
		
		auto& activeCamera = GetActiveCamera();
		if (activeCamera.GetMode() == Camera::Mode::Free)
		{
			UpdateFreecamMovement();
		}
		else if (activeCamera.GetMode() == Camera::Mode::Orbit)
		{
			UpdateOrbitCameraMovement();
		}
	}

	void CameraManager::Initialize()
	{
		Events::RegisterListener(EventType::OnFrame, [&]()
			{
				UpdateCameraFrame();
			}
		);

		Events::RegisterListener(EventType::OnDemoLoad, []()
			{
				// TODO: set initial coordinates of "free"-type cameras based on map
			}
		);
	}

	Camera& CameraManager::GetCamera(Camera::Mode mode)
	{
		for (auto& camera : cameras)
		{
			if (camera.GetMode() == mode)
			{
				return camera;
			}
		}

		throw std::runtime_error("Failed to find camera with desired mode");
	}

	void CameraManager::SetActiveCamera(Camera::Mode mode)
	{
		for (size_t i = 0; i < cameras.size(); i++)
		{
			if (cameras[i].GetMode() == mode)
			{
				activeCameraIndex = i;
			}
		}

		Events::Invoke(EventType::OnCameraChanged);
	}
}
