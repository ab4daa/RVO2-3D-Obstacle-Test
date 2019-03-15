/*place RVO.h under urho3d has error..*/
#include <random>
#include <RVO.h>
#include <Urho3D/Urho3DAll.h>

static unsigned int generate_random_seed()
{
	std::random_device rd;
	std::default_random_engine gen = std::default_random_engine(rd());
	std::uniform_int_distribution<unsigned int> dis(0, UINT_MAX);

	return dis(gen);
}

class MyApp : public Application
{
	URHO3D_OBJECT(MyApp, Application);

public:
	MyApp(Context* context) :
		Application(context)
	{}
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
	void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);
	

	SharedPtr<Scene> scene_;
	SharedPtr<Node> cameraNode_;
	/// Camera yaw angle.
	float yaw_{0.0f};
	/// Camera pitch angle.
	float pitch_{0.0f};
	MouseMode useMouseMode_;
	//for FPS
	float accTimeStep{0.0f};
	int FPS{0};
	Text* instructionText;
	void UpdateStaticModelGroup();

	//RVO
	Vector<RVO::Vector3> goals;
	RVO::RVOSimulator *sim;
	PODVector<StaticModelGroup*> groups;
	PODVector<Node*> boxes;
	void RVOsetupScenario(RVO::RVOSimulator *sim);
	void RVOsetPreferredVelocities(RVO::RVOSimulator *sim);
	void RVOsetupVisualizeScene(RVO::RVOSimulator *sim);
	void RVOupdateBoxPos(RVO::RVOSimulator *sim);
	int obstacle_num{ 50 };
};

void MyApp::Setup()
{
	// Called before engine initialization. engineParameters_ member variable can be modified here
	engineParameters_[EP_FULL_SCREEN] = false;
	SetRandomSeed(generate_random_seed());
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

	scene_->CreateComponent<DebugRenderer>();
	scene_->GetComponent<DebugRenderer>()->SetLineAntiAlias(true);

	// Create a directional light to the world so that we can see something. The light scene node's orientation controls the
	// light direction; we will use the SetDirection() function which calculates the orientation from a forward direction vector.
	// The light will use default settings (white light, no shadows)
	Node* lightNode = scene_->CreateChild("DirectionalLight");
	lightNode->SetDirection(Vector3(0.6f, -1.0f, 0.8f)); // The direction vector does not need to be normalized
	Light* light = lightNode->CreateComponent<Light>();
	light->SetLightType(LIGHT_DIRECTIONAL);

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

	// Subscribe HandlePostRenderUpdate() function for processing the post-render update event, during which we request debug geometry
	SubscribeToEvent(E_POSTRENDERUPDATE, URHO3D_HANDLER(MyApp, HandlePostRenderUpdate));
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
	UpdateStaticModelGroup();

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
	sim->setAgentDefaults(15.0f, 50.0f, 10, 10.0f, 5.0f, 1.5f, 2.0f);

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
		sim->addObstacle(RVO::Vector3(Random(-200.0f, 200.0f), Random(-200.0f, 200.0f), Random(-200.0f, 120.0f)), Random(10.0f, 30.0f));
	}
	sim->processObstacles();
}

void MyApp::RVOsetPreferredVelocities(RVO::RVOSimulator *sim)
{
	/* Set the preferred velocity to be a vector of unit magnitude (speed) in the direction of the goal. */
	for (size_t i = 0; i < sim->getNumAgents(); ++i) {
		RVO::Vector3 goalVector = goals[i] - sim->getAgentPosition(i);

//		if (RVO::absSq(goalVector) > 1.0f) {
//			goalVector = RVO::normalize(goalVector);
//		}

		sim->setAgentPrefVelocity(i, goalVector);
	}
}

void MyApp::RVOsetupVisualizeScene(RVO::RVOSimulator *sim)
{
	ResourceCache* cache = GetSubsystem<ResourceCache>();

	const unsigned grpNum = 512;
	Node * grp = scene_->CreateChild("group");
	StaticModelGroup * smg = grp->CreateComponent<StaticModelGroup>();
	smg->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
	smg->SetMaterial(cache->GetResource<Material>("Materials/Stone.xml"));
	groups.Push(smg);
	Urho3D::Vector3 hSize(cache->GetResource<Model>("Models/Box.mdl")->GetBoundingBox().HalfSize());
	float radius = hSize.Length();

	for (unsigned i = 0; i < sim->getNumAgents(); ++i)
	{
		RVO::Vector3 RVOpos = sim->getAgentPosition(i);
		Vector3 pos(RVOpos.x(), RVOpos.y(), RVOpos.z());

		Node* boxNode = scene_->CreateChild("Box");
		boxNode->SetTransform(pos, Quaternion::IDENTITY);
		boxNode->SetScale(1.0f);
		smg->AddInstanceNode(boxNode);
		boxes.Push(boxNode);
		
		sim->setAgentRadius(i, radius);

		if (smg->GetNumInstanceNodes() >= grpNum)
		{
			grp = scene_->CreateChild("group");
			smg = grp->CreateComponent<StaticModelGroup>();
			smg->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
			smg->SetMaterial(cache->GetResource<Material>("Materials/Stone.xml"));
			groups.Push(smg);
		}
	}

	/*obstacle*/
	for (unsigned i = 0; i < sim->getNumObstacles(); i++)
	{
		RVO::Vector3 RVOpos = sim->getObstaclePosition(i);
		Urho3D::Vector3 pos(RVOpos.x(), RVOpos.y(), RVOpos.z());
		float scale = sim->getObstacleRadius(i);

		Node* ballNode = scene_->CreateChild("ball");
		ballNode->SetTransform(pos, Quaternion::IDENTITY);
		ballNode->SetScale(scale);
		StaticModel* ballObject = ballNode->CreateComponent<StaticModel>();
		ballObject->SetModel(cache->GetResource<Model>("Models/Sphere.mdl"));
		Material * m = new Material(context_);
		m->SetNumTechniques(1);
		m->SetTechnique(0, cache->GetResource<Technique>("Techniques/NoTexture.xml"));
		m->SetShaderParameter("MatDiffColor", Vector4(Random(1.0f), Random(1.0f), Random(1.0f), 1.0f));
		m->SetShaderParameter("MatSpecColor", Vector4(0.5f, 0.5f, 0.5f, 16.0f));
		ballObject->SetMaterial(m);
	}
}

void MyApp::RVOupdateBoxPos(RVO::RVOSimulator *sim)
{
	for (unsigned i = 0; i < sim->getNumAgents(); ++i)
	{
		RVO::Vector3 RVOpos = sim->getAgentPosition(i);
		Vector3 pos(RVOpos.x(), RVOpos.y(), RVOpos.z());

		boxes[i]->SetPosition(pos);
	}
}

void MyApp::HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
	DebugRenderer* debug = scene_->GetComponent<DebugRenderer>();

	for (unsigned i = 0; i < sim->getNumObstacles(); ++i)
	{
		RVO::Vector3 RVOpos = sim->getObstaclePosition(i);
		Vector3 pos(RVOpos.x(), RVOpos.y(), RVOpos.z());
		float r = sim->getObstacleRadius(i);

		debug->AddSphere(Sphere(pos, r), Color::RED);
	}
}

static void updateStaticModelGroupWork(const WorkItem* item, unsigned threadIndex)
{
	StaticModelGroup ** start = reinterpret_cast<StaticModelGroup **>(item->start_);
	StaticModelGroup ** end = reinterpret_cast<StaticModelGroup **>(item->end_);

	while (1)
	{
		StaticModelGroup * s = *start;
		s->GetWorldBoundingBox();
		if (start == end)
			break;
		start++;
	}
}

void MyApp::UpdateStaticModelGroup()
{
	URHO3D_PROFILE(UpdateStaticModelGroup);
	WorkQueue* queue = GetSubsystem<WorkQueue>();
	int numWorkItems = queue->GetNumThreads() + 1;
	int unitsPerItem = Max(groups.Size() / numWorkItems, 1);

	PODVector<StaticModelGroup*>::Iterator start = groups.Begin();
	for (int i = 0; i < numWorkItems && groups.End() - start > 0; ++i)
	{
		PODVector<StaticModelGroup*>::Iterator end;
		if (i == numWorkItems - 1)
			end = groups.End() - 1;
		else
			end = start + unitsPerItem - 1;

		SharedPtr<WorkItem> item = queue->GetFreeItem();
		item->priority_ = M_MAX_UNSIGNED;
		item->workFunction_ = updateStaticModelGroupWork;
		item->aux_ = NULL;
		item->start_ = &(*start);
		item->end_ = &(*end);
		queue->AddWorkItem(item);

		start += unitsPerItem;
	}
	queue->Complete(M_MAX_UNSIGNED);
}

URHO3D_DEFINE_APPLICATION_MAIN(MyApp)
