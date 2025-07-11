#pragma once

#if MICRO_WITH_OPENVDB

#include <queue>
#include <Eigen/Core>
#include <Eigen/Sparse>
#include <openvdb/openvdb.h>

using namespace openvdb;

////////////////////////////////////////////////////////////////////////////////

struct myless {
    bool operator()(const Eigen::Vector3i &lhs, const Eigen::Vector3i &rhs) const 
    {
        if (lhs[0] != rhs[0])
            return lhs[0] < rhs[0];
        else if (lhs[1] != rhs[1])
            return lhs[1] < rhs[1];
        else if (lhs[2] != rhs[2])
            return lhs[2] < rhs[2];
        else
            return false;
    }
};

void read_mesh(const std::string &input, std::vector<Vec3s> &V, std::vector<Vec3I> &F);

template < typename DerivedC, typename DerivedK>
int connected_components(
  const Eigen::SparseMatrix<int> & A,
  Eigen::PlainObjectBase<DerivedC> & C,
  Eigen::PlainObjectBase<DerivedK> & K)
{
  typedef typename Eigen::SparseMatrix<int>::Index Index;
  const auto m = A.rows();
  assert(A.cols() == A.rows() && "A should be square");
  // 1.1 sec
  // m  means not yet visited
  C.setConstant(m,1,m);
  // Could use amortized dynamic array but didn't see real win.
  K.setZero(m,1);
  typename DerivedC::Scalar c = 0;
  for(Eigen::Index f = 0;f<m;f++)
  {
    // already seen
    if(C(f)<m) continue;
    // start bfs
    std::queue<Index> Q;
    Q.push(f);
    while(!Q.empty())
    {
      const Index g = Q.front();
      Q.pop();
      // already seen
      if(C(g)<m) continue;
      // see it
      C(g) = c;
      K(c)++;
      for(typename Eigen::SparseMatrix<int>::InnerIterator it (A,g); it; ++it)
      {
        const Index n = it.index();
        // already seen
        if(C(n)<m) continue;
        Q.push(n);
      }
    }
    c++;
  }
  K.conservativeResize(c,1);
  return c;
}

template < typename DerivedC, typename DerivedD >
void eigen_to_openvdb(const Eigen::Matrix<DerivedC, -1, -1> &mat, std::vector<openvdb::v10_0::math::Vec3<DerivedD>> &out)
{
    assert(mat.cols() == 3);
    out.clear();
    out.reserve(mat.rows());
    for (int i = 0; i < mat.rows(); i++)
        out.emplace_back(mat(i, 0), mat(i, 1), mat(i, 2));
}

template < typename DerivedC, typename DerivedD >
void openvdb_to_eigen(const std::vector<openvdb::v10_0::math::Vec3<DerivedC>> &in, Eigen::Matrix<DerivedD, -1, -1> &out)
{
    out.resize(in.size(), 3);
    for (int i = 0; i < in.size(); i++)
        for (int d = 0; d < 3; d++)
            out(i, d) = in[i](d);
}

FloatGrid::Ptr mesh2sdf(const std::string &path, const double voxel);

void volmesh2surface(const Eigen::MatrixXd& V, const Eigen::MatrixXi& T, std::vector<Vec3s> &v, std::vector<Vec3I> &f);

FloatGrid::Ptr volmesh2sdf(const Eigen::MatrixXd& V, const Eigen::MatrixXi& T, const double voxel);

void clean_quads(std::vector<Vec3I> &Tri, std::vector<Vec4I> &Quad);

void sdf2mesh(const FloatGrid::Ptr &grid, std::vector<Vec3s> &V, std::vector<Vec3I> &F, double adaptivity = 0);

void write_mesh(const std::string &output, const std::vector<Vec3s> &V, const std::vector<Vec3I> &Tri);

double signed_volume_of_triangle(const Vec3s &p1, const Vec3s &p2, const Vec3s &p3);

double compute_surface_mesh_volume(const std::vector<Vec3s> &V, const std::vector<Vec3I> &Tri);

double volume_after_offset(const FloatGrid::Ptr &grid, const double radius, std::vector<Vec3s> &Ve, std::vector<Vec3I> &Tri);

double cylinderSDF(
    const Eigen::Vector3d& bottom,
    const Eigen::Vector3d& top,
    const float radius,
    const Eigen::Vector3d& x);

FloatGrid::Ptr createLevelSetCylinder(
    const Eigen::Vector3d& bottom,
    const Eigen::Vector3d& top,
    const float radius = 1.0f,
    const float voxelSize = 0.1f,
    const float halfWidth = float(LEVEL_SET_HALF_WIDTH));

#endif