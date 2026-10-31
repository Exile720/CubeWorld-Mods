#include "nav_math.hpp"
#include <cstdlib>
#include <iostream>
namespace {
int count=0;
void check(bool ok,const char* label) { ++count;if(!ok){std::cerr<<"FAIL: "<<label<<'\n';std::exit(1);} }
bool near(double a,double b,double eps=1e-7){return std::abs(a-b)<eps;}
}
int main() {
    using namespace nav;
    Point origin{16777216.125,16777216.25,80};
    check(near(distance(origin,{origin.x+3,origin.y+4,999}),5),"distance uses horizontal plane at large coordinates");
    check(near(relativeBearing({},{0,-10,0},0),0),"forward at zero native yaw");
    check(near(relativeBearing({}, {-10,0,0},0),90),"target on screen right");
    check(near(relativeBearing({}, {10,0,0},0),-90),"target on screen left");
    check(near(relativeBearing({}, {10,0,0},90),0),"turning toward target recenters bearing");
    check(near(wrap(359),-1)&&near(wrap(-721),-1),"angle wrap across north");
    // Regression from the first real play session: the captured +0x44 matrix is affine.
    // Picking pixel (343.5,308.4) with it alone produced a destination only ~7 units away.
    // The expected offset below is independently solved from two perspective ray equations.
    const Matrix nativeView={.068605445,.038299914,.097218357,0,
                             .10449064,-.025146585,-.063830681,0,
                             0,.11630032,-.045817401,0,0,0,300,1};
    const double f=1/std::tan(pi/8);
    const Matrix nativePerspective={-f/(1463.0/843),0,0,0,0,f,0,0,0,0,1.0001,1,0,0,-.10001,0};
    Projection native;
    check(nativeMapProjection(nativePerspective,nativeView,origin,1463,843,native),"native perspective and view compose");
    Point clicked;Screen nativePixel;
    check(pick(native,{343.5,308.4},origin.z,clicked),"real map pixel can be unprojected");
    check(near(clicked.x-origin.x,1547.04913707534,1e-5)&&near(clicked.y-origin.y,509.19375964285,1e-5),"real map click has independently calculated world coordinates");
    check(near(distance(origin,clicked),1628.69251775305,1e-5),"distant map click remains distant instead of seven units away");
    check(project(native,clicked,nativePixel)&&near(nativePixel.x,343.5,1e-6)&&near(nativePixel.y,308.4,1e-6),"corrected destination projects to the original click");
    Projection invalid;
    check(!nativeMapProjection(nativeView,nativeView,origin,1463,843,invalid),"view-only transform cannot pass as perspective");
    double remaining=distance(origin,clicked);
    for(double progress:{.1,.25,.5,.75,.95}) {
        const Point moved{origin.x+(clicked.x-origin.x)*progress,origin.y+(clicked.y-origin.y)*progress,origin.z};
        const double now=distance(moved,clicked);
        check(now<remaining,"moving toward a fixed destination reduces remaining distance");remaining=now;
    }
    const Point away{origin.x-(clicked.x-origin.x)*.1,origin.y-(clicked.y-origin.y)*.1,origin.z};
    check(distance(away,clicked)>distance(origin,clicked),"moving away increases remaining distance");
    Matrix zero{},inverse{};check(!invert(zero,inverse),"singular projection rejected");
    for(double zoom:{0.01,0.35,1.0,10.0}) for(double angle:{0.0,0.6,2.7}) {
        const double c=std::cos(angle),s=std::sin(angle);
        Projection p;p.origin=origin;p.width=1920;p.height=1200;
        // Known homogeneous map projection with translation, tilt, perspective and camera depth.
        p.matrix={-zoom*c,zoom*0.7*s,0,0,zoom*s,zoom*0.7*c,0,0,0,-zoom*0.3,1,0.001,960*20,600*20,0,20};
        Point expected{origin.x+37.5,origin.y-128.25,origin.z};Screen pixel;Point actual;
        check(project(p,expected,pixel)&&pick(p,pixel,origin.z,actual),"map point can be selected");
        check(near(actual.x,expected.x,1e-5)&&near(actual.y,expected.y,1e-5),"zoom pan rotation round trip");
    }
    Projection p;p.width=100;p.height=100;p.matrix={1,0,0,0,0,1,0,0,0,0,1,0,50,50,0,1};
    Point result;check(!pick(p,{-1,50},0,result),"outside map click rejected");
    check(decode(encode(origin),result)&&near(result.x,origin.x),"waypoint persistence round trip");
    check(!decode("CW_WAYPOINT_9 1 2 3",result),"unsupported storage version rejected");
    check(!decode("CW_WAYPOINT_2 1 2 3 junk",result),"trailing corrupt storage rejected");
    check(!decode("CW_WAYPOINT_2 1e99 2 3",result),"invalid coordinate rejected");
    check(!decode("CW_WAYPOINT_1 1 2 3",result),"unreliable version-one markers require remarking");
    check(worldScope(10000,"A")!=worldScope(10000,"B"),"same-seed worlds have independent markers");
    check(worldScope(10000,"A")!=worldScope(10001,"A"),"different-seed worlds have independent markers");
    std::cout<<"PASS "<<count<<" navigation checks\n";
}
