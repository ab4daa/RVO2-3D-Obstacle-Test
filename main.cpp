/*place RVO.h under urho3d has error..*/
#include <time.h>
#include <RVO.h>
#if 0
#include <Urho3D/Engine/Application.h>
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/Engine/EngineDefs.h>
#include <Urho3D/Input/InputEvents.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#endif
#include <Urho3D/Engine/Application.h>
#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/Engine/EngineDefs.h>
#include <Urho3D/Graphics/AnimatedModel.h>
#include <Urho3D/Graphics/AnimationController.h>
#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Engine/DebugHud.h>
#include <Urho3D/Graphics/DebugRenderer.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/Light.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Graphics/Renderer.h>
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/Navigation/Obstacle.h>
#include <Urho3D/Navigation/OffMeshConnection.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>
#include <Urho3D/DebugNew.h>
#include <Urho3D/Container/Vector.h>

using namespace Urho3D;

class MyApp : public Application
{
	URHO3D_OBJECT(MyApp, Application);

public:
	MyApp(Context* context) :
		Application(context)
	{
		accTimeStep = 0.0f;
		FPS = 0;
		yaw_ = 0.0f;
		pitch_ = 0.0f;
		obstacle_num = 50;
	}
	virtual void Setup();
	virtual void Start();
	virtual void Stop();
	void HandleKeyDown(StringHash eventType, VariantMap& eventData);
	void CreateScene();
	void SetupViewport();
	void MoveCamera(float timeStep);
	void SubscribeToEvents();
	void HandleUpdate(StringHash eventType, VariantMap& eventData);
	void InitMouseMode(MouseMode mode);
	void CreateInstructions();
	void CreateConsoleAndDebugHud();
	

	SharedPtr<Scene> scene_;
	SharedPtr<Node> cameraNode_;
	/// Camera yaw angle.
	float yaw_;
	/// Camera pitch angle.
	float pitch_;
	MouseMode useMouseMode_;
	//for FPS
	float accTimeStep;
	int FPS;
	Text* instructionText;

	//RVO
	Vector<RVO::Vector3> goals;
	RVO::RVOSimulator *sim;
	Vector<Node*> boxes;
	void RVOsetupScenario(RVO::RVOSimulator *sim);
	void RVOsetPreferredVelocities(RVO::RVOSimulator *sim);
	void RVOsetupVisualizeScene(RVO::RVOSimulator *sim);
	void RVOupdateBoxPos(RVO::RVOSimulator *sim);
	int obstacle_num;
};

void MyApp::Setup()
{
	// Called before engine initialization. engineParameters_ member variable can be modified here
	engineParameters_[EP_FULL_SCREEN] = false;
	engineParameters_[EP_SOUND] = false;
	SetRandomSeed(time(NULL));
}

void MyApp::Start()
{
	CreateConsoleAndDebugHud();

	// Create the scene content
	CreateScene();

	// Setup the viewport for displaying the scene
	SetupViewport();

	SubscribeToEvents();
	// Called after engine initialization. Setup application & subscribe to events here
	SubscribeToEvent(E_KEYDOWN, URHO3D_HANDLER(MyApp, HandleKeyDown));
	InitMouseMode(MM_RELATIVE);

	sim = new RVO::RVOSimulator();
	RVOsetupScenario(sim);
	//visualize RVO
	RVOsetupVisualizeScene(sim);
	CreateInstructions();
}

void MyApp::Stop()
{
	// Perform optional cleanup after main loop has terminated
	delete sim;
}

void MyApp::HandleKeyDown(StringHash eventType, VariantMap& eventData)
{
	using namespace KeyDown;
	// Check for pressing ESC. Note the engine_ member variable for convenience access to the Engine object
	int key = eventData[P_KEY].GetInt();
	if (key == KEY_ESCAPE)
		engine_->Exit();
	else if(key == KEY_F2)
		GetSubsystem<DebugHud>()->ToggleAll();
}

void MyApp::CreateScene()
{
	ResourceCache* cache = GetSubsystem<ResourceCache>();

	scene_ = new Scene(context_);

	// Create the Octree component to the scene. This is required before adding any drawable components, or else nothing will
	// show up. The default octree volume will be from (-1000, -1000, -1000) to (1000, 1000, 1000) in world coordinates; it
	// is also legal to place objects outside the volume but their visibility can then not be checked in a hierarchically
	// optimizing manner
	scene_->CreateComponent<Octree>();

	// Create a child scene node (at world origin) and a StaticModel component into it. Set the StaticModel to show a simple
	// plane mesh with a "stone" material. Note that naming the scene nodes is optional. Scale the scene node larger
	// (100 x 100 world units)
	Node* planeNode = scene_->CreateChild("Plane");
	planeNode->SetScale(Vector3(100.0f, 1.0f, 100.0f));
	StaticModel* planeObject = planeNode->CreateComponent<StaticModel>();
	planeObject->SetModel(cache->GetResource<Model>("Models/Plane.mdl"));
	planeObject->SetMaterial(cache->GetResource<Material>("Materials/StoneTiled.xml"));

	// Create a directional light to the world so that we can see something. The light scene node's orientation controls the
	// light direction; we will use the SetDirection() function which calculates the orientation from a forward direction vector.
	// The light will use default settings (white light, no shadows)
	Node* lightNode = scene_->CreateChild("DirectionalLight");
	lightNode->SetDirection(Vector3(0.6f, -1.0f, 0.8f)); // The direction vector does not need to be normalized
	Light* light = lightNode->CreateComponent<Light>();
	light->SetLightType(LIGHT_DIRECTIONAL);

	// Create more StaticModel objects to the scene, randomly positioned, rotated and scaled. For rotation, we construct a
	// quaternion from Euler angles where the Y angle (rotation about the Y axis) is randomized. The mushroom model contains
	// LOD levels, so the StaticModel component will automatically select the LOD level according to the view distance (you'll
	// see the model get simpler as it moves further away). Finally, rendering a large number of the same object with the
	// same material allows instancing to be used, if the GPU supports it. This reduces the amount of CPU work in rendering the
	// scene.
	const unsigned NUM_OBJECTS = 200;
	for (unsigned i = 0; i < NUM_OBJECTS; ++i)
	{
		Node* mushroomNode = scene_->CreateChild("Mushroom");
		mushroomNode->SetPosition(Vector3(Random(90.0f) - 45.0f, 0.0f, Random(90.0f) - 45.0f));
		mushroomNode->SetRotation(Quaternion(0.0f, Random(360.0f), 0.0f));
		mushroomNode->SetScale(0.5f + Random(2.0f));
		StaticModel* mushroomObject = mushroomNode->CreateComponent<StaticModel>();
		mushroomObject->SetModel(cache->GetResource<Model>("Models/Mushroom.mdl"));
		mushroomObject->SetMaterial(cache->GetResource<Material>("Materials/Mushroom.xml"));
	}

	// Create a scene node for the camera, which we will move around
	// The camera will use default settings (1000 far clip distance, 45 degrees FOV, set aspect ratio automatically)
	cameraNode_ = scene_->CreateChild("Camera");
	cameraNode_->CreateComponent<Camera>();

	// Set an initial position for the camera scene node above the plane
	cameraNode_->SetPosition(Vector3(0.0f, 5.0f, 0.0f));
}

void MyApp::SetupViewport()
{
	Renderer* renderer = GetSubsystem<Renderer>();

	// Set up a viewport to the Renderer subsystem so that the 3D scene can be seen. We need to define the scene and the camera
	// at minimum. Additionally we could configure the viewport screen size and the rendering path (eg. forward / deferred) to
	// use, but now we just use full screen and default render path configured in the engine command line options
	SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
	renderer->SetViewport(0, viewport);
}

void MyApp::SubscribeToEvents()
{
	// Subscribe HandleUpdate() function for processing update events
	SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(MyApp, HandleUpdate));
}

void MyApp::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
	using namespace Update;

	// Take the frame time step, which is stored as a float
	float timeStep = eventData[P_TIMESTEP].GetFloat();

	// Move the camera, scale movement with time step
	MoveCamera(timeStep);

	//RVO
	RVOsetPreferredVelocities(sim);
	sim->setTimeStep(timeStep);
	sim->doStep();
	RVOupdateBoxPos(sim);

	//FPS
	accTimeStep += timeStep;
	FPS += 1;
	if (accTimeStep >= 1.0f)
	{
		instructionText->SetText(String("Total agents: ") + String(sim->getNumAgents()) + String(" FPS: ") + String(FPS));
		accTimeStep = 0.0f;
		FPS = 0;
	}
}

void MyApp::MoveCamera(float timeStep)
{
	// Do not move if the UI has a focused element (the console)
	if (GetSubsystem<UI>()->GetFocusElement())
		return;

	Input* input = GetSubsystem<Input>();

	// Movement speed as world units per second
	const float MOVE_SPEED = 20.0f;
	// Mouse sensitivity as degrees per pixel
	const float MOUSE_SENSITIVITY = 0.1f;

	// Use this frame's mouse motion to adjust camera node yaw and pitch. Clamp the pitch between -90 and 90 degrees
	IntVector2 mouseMove = input->GetMouseMove();
	yaw_ += MOUSE_SENSITIVITY * mouseMove.x_;
	pitch_ += MOUSE_SENSITIVITY * mouseMove.y_;
	pitch_ = Clamp(pitch_, -90.0f, 90.0f);

	// Construct new orientation for the camera scene node from yaw and pitch. Roll is fixed to zero
	cameraNode_->SetRotation(Quaternion(pitch_, yaw_, 0.0f));

	// Read WASD keys and move the camera scene node to the corresponding direction if they are pressed
	// Use the Translate() function (default local space) to move relative to the node's orientation.
	if (input->GetKeyDown(KEY_W))
		cameraNode_->Translate(Vector3::FORWARD * MOVE_SPEED * timeStep);
	if (input->GetKeyDown(KEY_S))
		cameraNode_->Translate(Vector3::BACK * MOVE_SPEED * timeStep);
	if (input->GetKeyDown(KEY_A))
		cameraNode_->Translate(Vector3::LEFT * MOVE_SPEED * timeStep);
	if (input->GetKeyDown(KEY_D))
		cameraNode_->Translate(Vector3::RIGHT * MOVE_SPEED * timeStep);
}

void MyApp::InitMouseMode(MouseMode mode)
{
	useMouseMode_ = mode;

	Input* input = GetSubsystem<Input>();

//	if (GetPlatform() != "Web")
//	{
		if (useMouseMode_ == MM_FREE)
			input->SetMouseVisible(true);

//		Console* console = GetSubsystem<Console>();
		if (useMouseMode_ != MM_ABSOLUTE)
		{
			input->SetMouseMode(useMouseMode_);
//			if (console && console->IsVisible())
//				input->SetMouseMode(MM_ABSOLUTE, true);
		}
//	}
//	else
//	{
//		input->SetMouseVisible(true);
//		SubscribeToEvent(E_MOUSEBUTTONDOWN, URHO3D_HANDLER(MyApp, HandleMouseModeRequest));
//		SubscribeToEvent(E_MOUSEMODECHANGED, URHO3D_HANDLER(MyApp, HandleMouseModeChange));
//	}
}

void MyApp::CreateInstructions()
{
	ResourceCache* cache = GetSubsystem<ResourceCache>();
	UI* ui = GetSubsystem<UI>();

	// Construct new Text object, set string to display and font to use
	instructionText = ui->GetRoot()->CreateChild<Text>();
	instructionText->SetText(String("Total agents: ").AppendWithFormat("%d", sim->getNumAgents()));
	instructionText->SetFont(cache->GetResource<Font>("Fonts/Anonymous Pro.ttf"), 15);

	// Position the text relative to the screen center
	instructionText->SetHorizontalAlignment(HA_CENTER);
	instructionText->SetVerticalAlignment(VA_CENTER);
	instructionText->SetPosition(0, ui->GetRoot()->GetHeight() / 2 - 30);
}

void MyApp::CreateConsoleAndDebugHud()
{
	// Get default style
	ResourceCache* cache = GetSubsystem<ResourceCache>();
	XMLFile* xmlFile = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");

	// Create console
	//Console* console = engine_->CreateConsole();
	//console->SetDefaultStyle(xmlFile);
	//console->GetBackground()->SetOpacity(0.8f);

	// Create debug HUD.
	DebugHud* debugHud = engine_->CreateDebugHud();
	debugHud->SetDefaultStyle(xmlFile);
}

void MyApp::RVOsetupScenario(RVO::RVOSimulator *sim)
{
	/* Specify the global time step of the simulation. */
	sim->setTimeStep(0.125f);

	/* Specify the default parameters for agents that are subsequently added. */
	sim->setAgentDefaults(15.0f, 10, 10.0f, 1.5f, 2.0f);

	/* Add agents, specifying their start position, and store their goals on the opposite side of the environment. */
	for (float a = 0; a < M_PI; a += 0.1f) {
		const float z = 100.0f * std::cos(a);
		const float r = 100.0f * std::sin(a);

		for (size_t i = 0; i < r / 2.5f; ++i) {
			const float x = r * std::cos(i * 2.0f * M_PI / (r / 2.5f));
			const float y = r * std::sin(i * 2.0f * M_PI / (r / 2.5f));

			sim->addAgent(RVO::Vector3(x, y, z));
			goals.Push(-sim->getAgentPosition(sim->getNumAgents() - 1));
		}
	}

	float edge = 300.0f;
	float zz = 120.0f;
	for (float xx = -edge; xx < edge; xx += 5.0f)
	{
		for (float yy = -edge; yy < edge; yy += 5.0f)
		{
			sim->addAgent(RVO::Vector3(xx, yy, zz));
			sim->setAgentMaxSpeed(sim->getNumAgents() - 1, 3.0f);
			goals.Push(-sim->getAgentPosition(sim->getNumAgents() - 1));
		}
	}

	/*obstacle*/	
	for (int ii = 0; ii < obstacle_num; ii++)
	{
		sim->addAgent(RVO::Vector3(Random(100.0f), Random(100.0f), Random(100.0f)));
		sim->setAgentMaxSpeed(sim->getNumAgents() - 1, 0.001f);
		goals.Push(sim->getAgentPosition(sim->getNumAgents() - 1));
	}
}

void MyApp::RVOsetPreferredVelocities(RVO::RVOSimulator *sim)
{
	/* Set the preferred velocity to be a vector of unit magnitude (speed) in the direction of the goal. */
	for (size_t i = 0; i < sim->getNumAgents() - obstacle_num; ++i) {
		RVO::Vector3 goalVector = goals[i] - sim->getAgentPosition(i);

//		if (RVO::absSq(goalVector) > 1.0f) {
//			goalVector = RVO::normalize(goalVector);
//		}

		sim->setAgentPrefVelocity(i, goalVector);
	}

	/*obstacle*/
	for (size_t i = sim->getNumAgents() - obstacle_num; i < sim->getNumAgents(); i++)
	{
		sim->setAgentPrefVelocity(i, RVO::Vector3(0.0f, 0.0f, 0.0f));
	}
}

void MyApp::RVOsetupVisualizeScene(RVO::RVOSimulator *sim)
{
	ResourceCache* cache = GetSubsystem<ResourceCache>();
	cache->AddResourceDir(String("F:/genei_model/Urho3D/ball"));

	for (unsigned i = 0; i < sim->getNumAgents() - obstacle_num; ++i)
	{
		RVO::Vector3 RVOpos = sim->getAgentPosition(i);
		Vector3 pos(RVOpos.x(), RVOpos.y(), RVOpos.z());

		Node* boxNode = scene_->CreateChild("Box");
		boxNode->SetPosition(pos);
		boxNode->SetRotation(Quaternion(0.0f, 0.0f, 0.0f));
		boxNode->SetScale(1.0f);
		StaticModel* boxObject = boxNode->CreateComponent<StaticModel>();
		boxObject->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
		boxObject->SetMaterial(cache->GetResource<Material>("Materials/Stone.xml"));
		boxes.Push(boxNode);

		/*find bounding sphere for RVO radius*/
		Urho3D::Vector3 bbSize = boxObject->GetModel()->GetBoundingBox().Size();
		float radius = 0.0f;
		for (int ii = 0; ii < 3; ii++)
		{
			if (bbSize.Data()[ii] > radius)
				radius = bbSize.Data()[ii];
		}
		radius /= 2.0f;
		sim->setAgentRadius(i, radius);
	}

	/*obstacle*/
	for (unsigned i = sim->getNumAgents() - obstacle_num; i < sim->getNumAgents(); i++)
	{
		RVO::Vector3 RVOpos = sim->getAgentPosition(i);
		Vector3 pos(RVOpos.x(), RVOpos.y(), RVOpos.z());
		float scale = Random(1.0f, 8.0f);

		Node* ballNode = scene_->CreateChild("ball");
		ballNode->SetPosition(pos);
		ballNode->SetRotation(Quaternion(0.0f, 0.0f, 0.0f));
		ballNode->SetScale(scale);
		StaticModel* ballObject = ballNode->CreateComponent<StaticModel>();
		ballObject->SetModel(cache->GetResource<Model>("ball.mdl"));
		ballObject->ApplyMaterialList("ball.txt");

		/*find bounding sphere for RVO radius*/
		Urho3D::Vector3 bbSize = ballObject->GetModel()->GetBoundingBox().Size();
		float radius = 0.0f;
		for (int ii = 0; ii < 3; ii++)
		{
			if (bbSize.Data()[ii] > radius)
				radius = bbSize.Data()[ii];
		}
		radius *= scale;
		radius /= 2.0f;		
		sim->setAgentRadius(i, radius + 2.0f);//add a bit more radius for obstacle
	}
}

void MyApp::RVOupdateBoxPos(RVO::RVOSimulator *sim)
{
	for (unsigned i = 0; i < sim->getNumAgents() - obstacle_num; ++i)
	{
		RVO::Vector3 RVOpos = sim->getAgentPosition(i);
		Vector3 pos(RVOpos.x(), RVOpos.y(), RVOpos.z());

		boxes[i]->SetPosition(pos);
	}

	for (unsigned i = sim->getNumAgents() - obstacle_num; i < sim->getNumAgents(); i++)
	{
		sim->setAgentPosition(i, goals[i]);
	}
}

URHO3D_DEFINE_APPLICATION_MAIN(MyApp)
