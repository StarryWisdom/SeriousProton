#include "Collisionable.h"
#include "Renderable.h"
#include "vectorUtils.h"

#define BOX2D_SCALE 20.0f
static inline sf::Vector2f b2v(b2Vec2 v)
{
    return sf::Vector2f(v.x * BOX2D_SCALE, v.y * BOX2D_SCALE);
}
static inline b2Vec2 v2b(sf::Vector2f v)
{
    return b2Vec2(v.x / BOX2D_SCALE, v.y / BOX2D_SCALE);
}

b2World* CollisionManager::world;

void CollisionManager::initialize()
{
    world = new b2World(b2Vec2(0, 0));
}

class QueryCallback : public b2QueryCallback
{
public:
    PVector<Collisionable> list;

	/// Called for each fixture found in the query AABB.
	/// @return false to terminate the query.
	virtual bool ReportFixture(b2Fixture* fixture)
	{
        P<Collisionable> ptr = (Collisionable*)fixture->GetBody()->GetUserData();
        if (ptr)
            list.push_back(ptr);
        return true;
	}
};

PVector<Collisionable> CollisionManager::queryArea(sf::Vector2f lowerBound, sf::Vector2f upperBound)
{
    QueryCallback callback;
    b2AABB aabb;
    aabb.lowerBound = v2b(lowerBound);
    aabb.upperBound = v2b(upperBound);
    world->QueryAABB(&callback, aabb);
    return callback.list;
}

class Collision
{
public:
    P<Collisionable> A;
    P<Collisionable> B;
    
    Collision(P<Collisionable> A, P<Collisionable> B)
    : A(A), B(B)
    {}
};

void CollisionManager::handleCollisions(float delta)
{
    if (delta <= 0.0)
        return;
    
    P<Collisionable> destroy = NULL;
    world->Step(delta, 4, 8);
    std::vector<Collision> collisions;
    for(b2Contact* contact = world->GetContactList(); contact; contact = contact->GetNext())
    {
        if (contact->IsTouching() && contact->IsEnabled())
        {
            Collisionable* A = (Collisionable*)contact->GetFixtureA()->GetBody()->GetUserData();
            Collisionable* B = (Collisionable*)contact->GetFixtureB()->GetBody()->GetUserData();
            if (!A->isDestroyed() && !B->isDestroyed())
            {
                collisions.push_back(Collision(A, B));
            }else{
                if (A->isDestroyed())
                    destroy = A;
                if (B->isDestroyed())
                    destroy = B;
            }
        }
    }

    for(unsigned int n=0; n<collisions.size(); n++)
    {
        Collisionable* A = *collisions[n].A;
        Collisionable* B = *collisions[n].B;
        if (A && B)
        {
            A->collision(B);
            B->collision(A);
        }
    }
    
    //Lazy cleanup of already destroyed bodies. We cannot destroy the bodies while we are walking trough the ContactList, as it would invalidate the contact we are iterating on.
    if (destroy)
    {
        world->DestroyBody(destroy->body);
        destroy->body = NULL;
    }
}

Collisionable::Collisionable(float radius)
{
    enablePhysics = false;
    staticPhysics = false;
    body = NULL;
    
    setCollisionRadius(radius);
}

Collisionable::Collisionable(sf::Vector2f boxSize, sf::Vector2f boxOrigin)
{
    enablePhysics = false;
    staticPhysics = false;
    body = NULL;
    
    setCollisionBox(boxSize, boxOrigin);
}

Collisionable::Collisionable(const std::vector<sf::Vector2f>& shape)
{
    enablePhysics = false;
    staticPhysics = false;
    body = NULL;
    
    setCollisionShape(shape);
}

Collisionable::~Collisionable()
{
    if (body)
        CollisionManager::world->DestroyBody(body);
}

void Collisionable::setCollisionRadius(float radius)
{
    b2CircleShape shape;
    shape.m_radius = radius / BOX2D_SCALE;

    createBody(&shape);
}

void Collisionable::setCollisionBox(sf::Vector2f boxSize, sf::Vector2f boxOrigin)
{
    b2PolygonShape shape;
    shape.SetAsBox(boxSize.x / 2.0 / BOX2D_SCALE, boxSize.y / 2.0 / BOX2D_SCALE, v2b(boxOrigin), 0);

    createBody(&shape);
}

void Collisionable::setCollisionShape(const std::vector<sf::Vector2f>& shapeList)
{
    for(unsigned int offset=1; offset<shapeList.size(); offset+=b2_maxPolygonVertices-2)
    {
        unsigned int len = b2_maxPolygonVertices;
        if (len > shapeList.size() - offset + 1)
            len = shapeList.size() - offset + 1;
        if (len < 3)
            break;
        
        b2Vec2 points[b2_maxPolygonVertices];
        points[0] = v2b(shapeList[0]);
        for(unsigned int n=0; n<len-1; n++)
            points[n+1] = v2b(shapeList[n+offset]);
        
        b2PolygonShape shape;
        bool valid = shape.Set(points, len);
        if (!valid)
        {
            shape.SetAsBox(1.0/BOX2D_SCALE, 1.0/BOX2D_SCALE, points[0], 0);
            printf("Failed to set valid collision shape: %i\n", int(shapeList.size()));
            for(unsigned int n=0; n<shapeList.size(); n++)
            {
                printf("%f %f\n", shapeList[n].x, shapeList[n].y);
            }
            destroy();
        }
        if (offset == 1)
        {
            createBody(&shape);
        }else{
            b2FixtureDef shapeDef;
            shapeDef.shape = &shape;
            shapeDef.density = 1.0;
            shapeDef.friction = 0.1;
            shapeDef.isSensor = !enablePhysics;
            body->CreateFixture(&shapeDef);
        }
    }
}

void Collisionable::setCollisionPhysics(bool enablePhysics, bool staticPhysics)
{
    this->enablePhysics = enablePhysics;
    this->staticPhysics = staticPhysics;

    if (!body)
        return;

    for(b2Fixture* f = body->GetFixtureList(); f; f = f->GetNext())
        f->SetSensor(!enablePhysics);
    body->SetType(staticPhysics ? b2_kinematicBody : b2_dynamicBody);
}

void Collisionable::createBody(b2Shape* shape)
{
    if (body)
    {
        while(body->GetFixtureList())
            body->DestroyFixture(body->GetFixtureList());
    }else{
        b2BodyDef bodyDef;
        bodyDef.type = staticPhysics ? b2_kinematicBody : b2_dynamicBody;
        bodyDef.userData = this;
        bodyDef.allowSleep = false;
        body = CollisionManager::world->CreateBody(&bodyDef);
    }
    
    b2FixtureDef shapeDef;
    shapeDef.shape = shape;
    shapeDef.density = 1.0;
    shapeDef.friction = 0.1;
    shapeDef.isSensor = !enablePhysics;
    body->CreateFixture(&shapeDef);
}

void Collisionable::collision(Collisionable* target)
{
}

void Collisionable::setPosition(sf::Vector2f position)
{
    if (body == NULL) return;
    body->SetTransform(v2b(position), body->GetAngle());
}

sf::Vector2f Collisionable::getPosition()
{
    if (body == NULL) return sf::Vector2f(0, 0);
    return b2v(body->GetPosition());
}

void Collisionable::setRotation(float angle)
{
    if (body == NULL) return;
    body->SetTransform(body->GetPosition(), angle / 180.0 * M_PI);
}

float Collisionable::getRotation()
{
    if (body == NULL) return 0;
    return body->GetAngle() / M_PI * 180.0;
}

void Collisionable::setVelocity(sf::Vector2f velocity)
{
    if (body == NULL) return;
    body->SetLinearVelocity(v2b(velocity));
}
sf::Vector2f Collisionable::getVelocity()
{
    if (body == NULL) return sf::Vector2f(0, 0);
    return b2v(body->GetLinearVelocity());
}

void Collisionable::setAngularVelocity(float velocity)
{
    if (body == NULL) return;
    body->SetAngularVelocity(velocity / 180.0 * M_PI);
}
float Collisionable::getAngularVelocity()
{
    if (body == NULL) return 0;
    return body->GetAngularVelocity() / M_PI * 180.0;
}

void Collisionable::applyImpulse(sf::Vector2f position, sf::Vector2f impulse)
{
    if (body == NULL) return;
    body->ApplyLinearImpulse(v2b(impulse), v2b(position), true);
}

sf::Vector2f Collisionable::toLocalSpace(sf::Vector2f v)
{
    if (body == NULL) return sf::Vector2f(0, 0);
    return b2v(body->GetLocalPoint(v2b(v)));
}
sf::Vector2f Collisionable::toWorldSpace(sf::Vector2f v)
{
    if (body == NULL) return sf::Vector2f(0, 0);
    return b2v(body->GetWorldPoint(v2b(v)));
}

std::vector<sf::Vector2f> Collisionable::getCollisionShape()
{
    std::vector<sf::Vector2f> ret;
    if (body == NULL) return ret;
    b2Fixture* f = body->GetFixtureList();
    b2Shape* s = f->GetShape();
    switch(s->GetType())
    {
    case b2Shape::e_circle:
        {
            b2CircleShape* cs = static_cast<b2CircleShape*>(s);
            float radius = cs->m_radius * BOX2D_SCALE;
            for(int n=0; n<32; n++)
                ret.push_back(sf::Vector2f(sin(float(n)/32.0*M_PI*2) * radius, cos(float(n)/32.0*M_PI*2) * radius));
        }
        break;
    case b2Shape::e_polygon:
        {
            b2PolygonShape* cs = static_cast<b2PolygonShape*>(s);
            for(int n=0; n<cs->GetVertexCount(); n++)
                ret.push_back(b2v(cs->GetVertex(n)));
        }
        break;
    default:
        break;
    }
    return ret;
}

#ifdef DEBUG
CollisionDebugDraw::CollisionDebugDraw(RenderLayer* layer)
: Renderable(layer)
{
    SetFlags(e_shapeBit | e_jointBit | e_centerOfMassBit);
    CollisionManager::world->SetDebugDraw(this);
}
    
void CollisionDebugDraw::render(sf::RenderTarget& window)
{
    renderTarget = &window;
    CollisionManager::world->DrawDebugData();
}

/// Draw a closed polygon provided in CCW order.
void CollisionDebugDraw::DrawPolygon(const b2Vec2* vertices, int32 vertexCount, const b2Color& color)
{
    sf::VertexArray a(sf::LinesStrip, vertexCount+1);
    for(int32 n=0; n<vertexCount; n++)
    {
        a[n].position = b2v(vertices[n]);
        a[n].color = sf::Color(color.r * 255, color.g * 255, color.b * 255, color.a * 255);
    }
    a[vertexCount].position = b2v(vertices[0]);
    a[vertexCount].color = sf::Color(color.r * 255, color.g * 255, color.b * 255, color.a * 255);
    renderTarget->draw(a);
}

/// Draw a solid closed polygon provided in CCW order.
void CollisionDebugDraw::DrawSolidPolygon(const b2Vec2* vertices, int32 vertexCount, const b2Color& color)
{
    sf::VertexArray a(sf::LinesStrip, vertexCount+1);
    for(int32 n=0; n<vertexCount; n++)
    {
        a[n].position = b2v(vertices[n]);
        a[n].color = sf::Color(color.r * 255, color.g * 255, color.b * 255, color.a * 255);
    }
    a[vertexCount].position = b2v(vertices[0]);
    a[vertexCount].color = sf::Color(color.r * 255, color.g * 255, color.b * 255, color.a * 255);
    renderTarget->draw(a);
}

/// Draw a circle.
void CollisionDebugDraw::DrawCircle(const b2Vec2& center, float32 radius, const b2Color& color)
{
    sf::CircleShape shape(radius * BOX2D_SCALE, 16);
    shape.setOrigin(radius * BOX2D_SCALE, radius * BOX2D_SCALE);
    shape.setPosition(b2v(center));
    shape.setFillColor(sf::Color::Transparent);
    shape.setOutlineColor(sf::Color(color.r * 255, color.g * 255, color.b * 255, color.a * 255));
    shape.setOutlineThickness(0.3);
    renderTarget->draw(shape);
}

/// Draw a solid circle.
void CollisionDebugDraw::DrawSolidCircle(const b2Vec2& center, float32 radius, const b2Vec2& axis, const b2Color& color)
{
    sf::CircleShape shape(radius * BOX2D_SCALE, 16);
    shape.setOrigin(radius * BOX2D_SCALE, radius * BOX2D_SCALE);
    shape.setPosition(b2v(center));
    shape.setFillColor(sf::Color::Transparent);
    shape.setOutlineColor(sf::Color(color.r * 255, color.g * 255, color.b * 255, color.a * 255));
    shape.setOutlineThickness(0.3);
    renderTarget->draw(shape);
}

/// Draw a line segment.
void CollisionDebugDraw::DrawSegment(const b2Vec2& p1, const b2Vec2& p2, const b2Color& color)
{
    sf::VertexArray a(sf::Lines, 2);
    a[0].position = b2v(p1);
    a[0].color = sf::Color(color.r * 255, color.g * 255, color.b * 255, color.a * 255);
    a[1].position = b2v(p2);
    a[1].color = sf::Color(color.r * 255, color.g * 255, color.b * 255, color.a * 255);
    renderTarget->draw(a);
}

/// Draw a transform. Choose your own length scale.
/// @param xf a transform.
void CollisionDebugDraw::DrawTransform(const b2Transform& xf)
{
    sf::VertexArray a(sf::Lines, 4);
    a[0].position = b2v(xf.p);
    a[1].position = b2v(xf.p) + sf::Vector2f(xf.q.GetXAxis().x * 10, xf.q.GetXAxis().y * 10);
    a[0].position = b2v(xf.p);
    a[1].position = b2v(xf.p) + sf::Vector2f(xf.q.GetYAxis().x * 10, xf.q.GetYAxis().y * 10);
    renderTarget->draw(a);
}
#endif//DEBUG