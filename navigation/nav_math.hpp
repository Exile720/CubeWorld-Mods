#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

namespace nav {
constexpr double pi = 3.14159265358979323846;
struct Point { double x=0, y=0, z=0; };
struct Screen { double x=0, y=0; };
using Matrix = std::array<double,16>;
inline bool finite(Point p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
        std::abs(p.x)<1e9 && std::abs(p.y)<1e9 && std::abs(p.z)<1e7;
}
inline double wrap(double degrees) {
    degrees=std::fmod(degrees+180.0,360.0);
    if(degrees<0) degrees+=360.0;
    return degrees-180.0;
}
inline double distance(Point a,Point b) { return std::hypot(b.x-a.x,b.y-a.y); }
// Native camera: yaw=0 faces -Y; screen-right is -X. Native positive yaw turns left.
inline double bearing(Point a,Point b) { return wrap(std::atan2(a.x-b.x,a.y-b.y)*180.0/pi); }
inline double relativeBearing(Point a,Point b,double cameraYaw) { return wrap(bearing(a,b)+cameraYaw); }
inline bool invert(const Matrix& m,Matrix& result) {
    double a[4][8]{};
    for(int row=0;row<4;++row) for(int col=0;col<4;++col) {
        a[row][col]=m[col*4+row]; a[row][col+4]=(row==col)?1.0:0.0;
        if(!std::isfinite(a[row][col])) return false;
    }
    for(int col=0;col<4;++col) {
        int pivot=col;
        for(int r=col+1;r<4;++r) if(std::abs(a[r][col])>std::abs(a[pivot][col])) pivot=r;
        if(std::abs(a[pivot][col])<1e-12) return false;
        for(int k=0;k<8;++k) std::swap(a[pivot][k],a[col][k]);
        const double d=a[col][col]; for(double& value:a[col]) value/=d;
        for(int r=0;r<4;++r) if(r!=col) {
            const double f=a[r][col];for(int k=0;k<8;++k) a[r][k]-=f*a[col][k];
        }
    }
    for(int r=0;r<4;++r) for(int c=0;c<4;++c) result[c*4+r]=a[r][c+4];
    return true;
}
inline std::array<double,4> transform(const Matrix& m,double x,double y,double z,double w=1.0) {
    std::array<double,4> out{};
    for(int r=0;r<4;++r) out[r]=m[r]*x+m[4+r]*y+m[8+r]*z+m[12+r]*w;
    return out;
}
inline Matrix multiply(const Matrix& a,const Matrix& b) {
    Matrix out{};
    for(int col=0;col<4;++col) for(int row=0;row<4;++row)
        for(int k=0;k<4;++k) out[col*4+row]+=a[k*4+row]*b[col*4+k];
    return out;
}
struct Projection {
    Matrix matrix{};
    Point origin{};
    double width=0,height=0;
    // Pixel transforms are used only by synthetic checks. The native map uses clip coordinates.
    bool pixels=true;
};
inline bool nativeMapProjection(const Matrix& perspective,const Matrix& view,Point origin,
                                double width,double height,Projection& out) {
    if(width<=0||height<=0||!finite(origin)) return false;
    for(double v:perspective) if(!std::isfinite(v)) return false;
    for(double v:view) if(!std::isfinite(v)) return false;
    // A view matrix alone has w=1; it must not be treated as the map's perspective projection.
    if(std::abs(perspective[11])<0.01||std::abs(perspective[15])>1e-4) return false;
    if(std::abs(view[3])+std::abs(view[7])+std::abs(view[11])>1e-4||std::abs(view[15]-1)>1e-4) return false;
    Projection next;next.matrix=multiply(perspective,view);next.origin=origin;
    next.width=width;next.height=height;next.pixels=false;
    Matrix inv{};if(!invert(next.matrix,inv)) return false;
    out=next;return true;
}
inline bool project(const Projection& p,Point world,Screen& screen) {
    if(!finite(world)||p.width<=0||p.height<=0) return false;
    const auto q=transform(p.matrix,world.x-p.origin.x,world.y-p.origin.y,world.z-p.origin.z);
    if(!std::isfinite(q[3])||q[3]<=1e-9) return false;
    const double x=q[0]/q[3],y=q[1]/q[3];
    screen=p.pixels?Screen{x,y}:Screen{(x+1)*p.width/2,(1-y)*p.height/2};
    return std::isfinite(screen.x)&&std::isfinite(screen.y);
}
// Intersect a screen ray with the horizontal map plane. This is continuous in zoom/pan.
inline bool pick(const Projection& p,Screen screen,double absoluteZ,Point& world) {
    if(p.width<=0||p.height<=0||screen.x<0||screen.y<0||screen.x>=p.width||screen.y>=p.height) return false;
    Matrix inv{};if(!invert(p.matrix,inv)) return false;
    double x=screen.x,y=screen.y;
    if(!p.pixels) {x=x/p.width*2-1;y=1-y/p.height*2;}
    auto a=transform(inv,x,y,0.0),b=transform(inv,x,y,0.5);
    if(std::abs(a[3])<1e-12||std::abs(b[3])<1e-12) return false;
    for(int i=0;i<3;++i) {a[i]/=a[3];b[i]/=b[3];}
    const double dz=b[2]-a[2];if(std::abs(dz)<1e-12) return false;
    const double t=(absoluteZ-p.origin.z-a[2])/dz;
    world={p.origin.x+a[0]+t*(b[0]-a[0]),p.origin.y+a[1]+t*(b[1]-a[1]),absoluteZ};
    return t>=0 && finite(world) && distance(p.origin,world)<1e6;
}
inline std::string encode(Point p) {
    std::ostringstream s;s.imbue(std::locale::classic());
    s<<"CW_WAYPOINT_2 "<<std::setprecision(17)<<p.x<<' '<<p.y<<' '<<p.z;return s.str();
}
inline bool decode(const std::string& data,Point& p) {
    if(data.size()>256) return false;
    std::istringstream s(data);s.imbue(std::locale::classic());std::string magic,extra;Point candidate;
    if(!(s>>magic>>candidate.x>>candidate.y>>candidate.z)||magic!="CW_WAYPOINT_2"||!finite(candidate)||(s>>extra)) return false;
    p=candidate;return true;
}
inline unsigned long long hashName(const std::string& name) {
    unsigned long long hash=14695981039346656037ull;
    for(unsigned char c:name){hash^=c;hash*=1099511628211ull;}return hash;
}
inline std::string worldScope(unsigned seed,const std::string& name) {
    std::ostringstream s;s<<"world_"<<seed<<'_'<<std::hex<<hashName(name);return s.str();
}
}
