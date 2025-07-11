#include "VDBTools.hh"

#if MICRO_WITH_OPENVDB

#include <igl/readOBJ.h>
#include <igl/readMSH.h>
#include <igl/barycenter.h>
#include <igl/adjacency_matrix.h>
#include <igl/writeOBJ.h>
#include <igl/bounding_box_diagonal.h>
#include <igl/remove_unreferenced.h>
#include <igl/boundary_facets.h>

#include <openvdb/tools/SignedFloodFill.h>
#include <openvdb/tools/LevelSetFilter.h>
#include <openvdb/tools/LevelSetPlatonic.h>
#include <openvdb/tools/MeshToVolume.h>
#include <openvdb/tools/VolumeToMesh.h>
#include <openvdb/tools/Composite.h>
#include <openvdb/tools/LevelSetMeasure.h>

void read_mesh(const std::string &input, std::vector<Vec3s> &V, std::vector<Vec3I> &F)
{
    Eigen::MatrixXd v;
    Eigen::MatrixXi f;
    igl::readOBJ(input, v, f);

    V.reserve(v.rows());
    F.reserve(f.rows());

    for (int i = 0; i < v.rows(); i++)
        V.emplace_back(v(i, 0), v(i, 1), v(i, 2));
    
    for (int j = 0; j < f.rows(); j++)
        F.emplace_back(f(j, 0), f(j, 1), f(j, 2));
}

FloatGrid::Ptr mesh2sdf(const std::string &path, const double voxel)
{
    Eigen::MatrixXd v;
    Eigen::MatrixXi f;
    igl::readOBJ(path, v, f);

    Eigen::SparseMatrix<int> adj;
    igl::adjacency_matrix(f, adj);

    Eigen::VectorXi C, K;
    int nc = connected_components(adj, C, K);

    Eigen::Index maxv;
    v.rowwise().squaredNorm().maxCoeff(&maxv);

    std::vector<Vec3s> SV;
    std::vector<Vec3I> SF;

    for (int i = 0; i < v.rows(); i++)
        SV.emplace_back(v(i,0),v(i,1),v(i,2));

    for (int i = 0; i < f.rows(); i++)
        if (C[f(i,0)] == C[maxv])
            SF.emplace_back(f(i,0),f(i,1),f(i,2));

    math::Transform::Ptr xform = math::Transform::createLinearTransform(voxel);
    FloatGrid::Ptr grid = tools::meshToLevelSet<FloatGrid>(*xform, SV, SF, 3);

    for (int k = 0; k < K.size(); k++)
    {
        if (k == C[maxv])
            continue;
        
        SF.clear();
        for (int i = 0; i < f.rows(); i++)
            if (C[f(i,0)] == k)
                SF.emplace_back(f(i,0),f(i,1),f(i,2));

        math::Transform::Ptr xform = math::Transform::createLinearTransform(voxel);
        FloatGrid::Ptr tmp_grid = tools::meshToLevelSet<FloatGrid>(*xform, SV, SF, 3);

        openvdb::tools::csgDifference(*grid, *tmp_grid);
    }

    return grid;
}

void volmesh2surface(const Eigen::MatrixXd& V, const Eigen::MatrixXi& T, std::vector<Vec3s> &v, std::vector<Vec3I> &f)
{
    Eigen::MatrixXi F;
    igl::boundary_facets(T, F);

    Eigen::MatrixXi subF;
    Eigen::MatrixXd subV;
    Eigen::VectorXi tmpI;
    igl::remove_unreferenced(V, F, subV, subF, tmpI);

    eigen_to_openvdb(subV, v);
    eigen_to_openvdb(subF, f);
}

FloatGrid::Ptr volmesh2sdf(const Eigen::MatrixXd& V, const Eigen::MatrixXi& T, const double voxel)
{
    std::vector<Vec3s> subV_openvdb;
    std::vector<Vec3I> subF_openvdb;
    volmesh2surface(V, T, subV_openvdb, subF_openvdb);

    math::Transform::Ptr xform = math::Transform::createLinearTransform(voxel);
    FloatGrid::Ptr grid = tools::meshToLevelSet<FloatGrid>(*xform, subV_openvdb, subF_openvdb, 3);

    return grid;
}

void clean_quads(std::vector<Vec3I> &Tri, std::vector<Vec4I> &Quad)
{
    for (const auto &f : Quad)
    {
        Tri.emplace_back(f(0), f(1), f(2));
        Tri.emplace_back(f(0), f(2), f(3));
    }

    Quad.clear();
}

void sdf2mesh(const FloatGrid::Ptr &grid, std::vector<Vec3s> &V, std::vector<Vec3I> &F, double adaptivity)
{
    std::vector<Vec4I> Quad;
    tools::volumeToMesh(*grid, V, F, Quad, 0, adaptivity, true);
    clean_quads(F, Quad);
}

void write_mesh(const std::string &output, const std::vector<Vec3s> &V, const std::vector<Vec3I> &Tri)
{
    FILE * obj_file = fopen(output.c_str(),"w");
    
    for(int i = 0;i<(int)V.size();i++)
    {
        fprintf(obj_file,"v");
        for(int j = 0;j<3;++j)
        {
        fprintf(obj_file," %0.17g", V[i](j));
        }
        fprintf(obj_file,"\n");
    }

    for(int i = 0;i<(int)Tri.size();++i)
    {
        fprintf(obj_file,"f");
        for(int j = 0; j<3;++j)
        {
        // OBJ is 1-indexed
        fprintf(obj_file," %u",Tri[i](2 - j)+1);
        }
        fprintf(obj_file,"\n");
    }

    // for(int i = 0;i<(int)Quad.size();++i)
    // {
    //     fprintf(obj_file,"f");
    //     for(int j = 0; j<4;++j)
    //     {
    //     // OBJ is 1-indexed
    //     fprintf(obj_file," %u",Quad[i](j)+1);
    //     }
    //     fprintf(obj_file,"\n");
    // }

    fclose(obj_file);
}

double signed_volume_of_triangle(const Vec3s &p1, const Vec3s &p2, const Vec3s &p3) {
    double v321 = p3(0)*p2(1)*p1(2);
    double v231 = p2(0)*p3(1)*p1(2);
    double v312 = p3(0)*p1(1)*p2(2);
    double v132 = p1(0)*p3(1)*p2(2);
    double v213 = p2(0)*p1(1)*p3(2);
    double v123 = p1(0)*p2(1)*p3(2);
    return (1 / 6.0)*(-v321 + v231 + v312 - v132 - v213 + v123);
}

double compute_surface_mesh_volume(const std::vector<Vec3s> &V, const std::vector<Vec3I> &Tri)
{
    Vec3s center(0, 0, 0);
    for (const auto &v : V)
        center += v;
    center /= V.size();
    
    double vol = 0;
    for (const auto &f : Tri)
        vol += signed_volume_of_triangle(V[f(0)] - center, V[f(1)] - center, V[f(2)] - center);

    return vol;
}

double volume_after_offset(const FloatGrid::Ptr &grid, const double radius, std::vector<Vec3s> &Ve, std::vector<Vec3I> &Tri)
{
    // erode
    auto tmp_grid = grid->deepCopyGrid();
    std::unique_ptr<tools::LevelSetFilter<FloatGrid>> filter = std::make_unique<tools::LevelSetFilter<FloatGrid>>(*gridPtrCast<FloatGrid>(tmp_grid));
    filter->offset(radius);

    // ls2mesh
    Ve.clear();
    Tri.clear();
    sdf2mesh(gridPtrCast<FloatGrid>(tmp_grid), Ve, Tri);

    double vol = abs(compute_surface_mesh_volume(Ve, Tri));
        
    return vol;
}

double cylinderSDF(
    const Eigen::Vector3d& bottom,
    const Eigen::Vector3d& top,
    const float radius,
    const Eigen::Vector3d& x)
{
    Eigen::Vector3d direct = top - bottom;
    const double height = direct.norm();
    direct /= height;

    Eigen::Vector3d vec = x - bottom;
    const double alpha = vec.dot(direct);
    
    return std::max(std::max(-alpha, alpha - height), (vec - alpha * direct).norm() - radius);
}

FloatGrid::Ptr createLevelSetCylinder(
    const Eigen::Vector3d& bottom,
    const Eigen::Vector3d& top,
    const float radius,
    const float voxelSize,
    const float halfWidth)
{
    Eigen::Vector3f bbox_min;
    bbox_min << std::min(bottom(0),top(0)) - radius,std::min(bottom(1),top(1)) - radius,std::min(bottom(2),top(2)) - radius;
    Eigen::Vector3f bbox_max;
    bbox_max << std::max(bottom(0),top(0)) + radius,std::max(bottom(1),top(1)) + radius,std::max(bottom(2),top(2)) + radius;

    Eigen::Vector3i bbox_int_min(floor(bbox_min(0) / voxelSize), floor(bbox_min(1) / voxelSize), floor(bbox_min(2) / voxelSize));
    Eigen::Vector3i bbox_int_max(ceil(bbox_max(0) / voxelSize), ceil(bbox_max(1) / voxelSize), ceil(bbox_max(2) / voxelSize));

    const float bg_value = halfWidth * voxelSize;
    openvdb::FloatGrid::Ptr grid = openvdb::FloatGrid::create(bg_value);
    grid->setTransform(math::Transform::createLinearTransform(voxelSize));
    openvdb::FloatGrid::Accessor accessor = grid->getAccessor();

    // Eigen::Vector3d direct = top - bottom;
    // const double height = direct.norm();
    // direct /= height;

    // Compute the signed distance from the surface of the sphere of each
    // voxel within the bounding box and insert the value into the grid
    // if it is smaller in magnitude than the background value.
    openvdb::Coord ijk;
    int &i = ijk[0], &j = ijk[1], &k = ijk[2];
    for (i = bbox_int_min(0); i <= bbox_int_max(0); ++i) 
    {
        const float x = i * voxelSize;
        for (j = bbox_int_min(1); j <= bbox_int_max(1); ++j) 
        {
            const float y = j * voxelSize;
            for (k = bbox_int_min(2); k <= bbox_int_max(2); ++k) 
            {
                const float z = k * voxelSize;
                // Eigen::Vector3d pos = Eigen::Vector3d(x, y, z) - bottom;
                // const double alpha = pos.dot(direct);
                // // sdf of side surface
                // const float dist1 = (pos - alpha * direct).norm() - radius;
                // // sdf of top and bottom
                // const float dist2 = -std::min(alpha, height - alpha);

                // const float dist = std::max(dist1, dist2);
                const double dist = cylinderSDF(bottom, top, radius, Eigen::Vector3d(x, y, z));
                // Only insert distances that are smaller in magnitude than
                // the background value.
                if (abs(dist) > bg_value) continue;
                // Set the distance for voxel (i,j,k).
                accessor.setValue(ijk, dist);
                // std::cout << "[" << i << "," << j << "," << k << "] " << dist << "\n";
            }
        }
    }
    // Propagate the outside/inside sign information from the narrow band
    // throughout the grid.
    openvdb::tools::signedFloodFill(grid->tree());

    return grid;
}

#endif