//
// Copyright (c) 2017-2020 CNRS INRIA
//

#ifndef __pinocchio_rnea_derivatives_faster_hxx__
#define __pinocchio_rnea_derivatives_faster_hxx__

#include "pinocchio/multibody/visitor.hpp"
#include "pinocchio/algorithm/check.hpp"

#include <iostream>

namespace pinocchio
{
  
  
  template<typename Scalar, int Options, template<typename,int> class JointCollectionTpl, typename ConfigVectorType, typename TangentVectorType1, typename TangentVectorType2>
  struct ComputeRNEADerivativesFasterForwardStep
  : public fusion::JointUnaryVisitorBase< ComputeRNEADerivativesFasterForwardStep<Scalar,Options,JointCollectionTpl,ConfigVectorType,TangentVectorType1,TangentVectorType2> >
  {
    typedef ModelTpl<Scalar,Options,JointCollectionTpl> Model;
    typedef DataTpl<Scalar,Options,JointCollectionTpl> Data;
    
    typedef boost::fusion::vector<const Model &,
                                  Data &,
                                  const ConfigVectorType &,
                                  const TangentVectorType1 &,
                                  const TangentVectorType2 &
                                  > ArgsType;
    
    template<typename JointModel>
    static void algo(const JointModelBase<JointModel> & jmodel,
                     JointDataBase<typename JointModel::JointDataDerived> & jdata,
                     const Model & model,
                     Data & data,
                     const Eigen::MatrixBase<ConfigVectorType> & q,
                     const Eigen::MatrixBase<TangentVectorType1> & v,
                     const Eigen::MatrixBase<TangentVectorType2> & a)
    {
      typedef typename Model::JointIndex JointIndex;
      typedef typename Data::Motion Motion;
      typedef typename Data::Inertia Inertia;

      const JointIndex & i = jmodel.id();
      const JointIndex & parent = model.parents[i];
      Motion & ov = data.ov[i];
      Motion & oa = data.oa[i];
      Motion & vJ = data.vJ[i];

      jmodel.calc(jdata.derived(),q.derived(),v.derived());
      
      data.liMi[i] = model.jointPlacements[i]*jdata.M();

      if(parent > 0)
      {
        data.oMi[i] = data.oMi[parent] * data.liMi[i];
        ov = data.ov[parent];
        oa = data.oa[parent];
      }
      else
      {
        data.oMi[i] = data.liMi[i];
        ov.setZero();
        oa = -model.gravity;
      }
  
      typedef typename SizeDepType<JointModel::NV>::template ColsReturn<typename Data::Matrix6x>::Type ColsBlock;
      ColsBlock J_cols = jmodel.jointCols(data.J);
      ColsBlock dJ_cols = jmodel.jointCols(data.dJ);
      ColsBlock ddJ_cols = jmodel.jointCols(data.ddJ);
      ColsBlock vdJ_cols = jmodel.jointCols(data.vdJ);

      // J and vJ
      J_cols.noalias() = data.oMi[i].act(jdata.S());
      vJ     = data.oMi[i].act( jdata.v() );

      // dJ
      motionSet::motionAction( ov , J_cols, dJ_cols );

      // ddJ
      motionSet::motionAction( oa , J_cols, ddJ_cols);
      motionSet::motionAction<ADDTO>( ov , dJ_cols,ddJ_cols);

      // vdJ
      motionSet::motionAction( vJ ,J_cols, vdJ_cols );
      vdJ_cols.noalias() += 2*dJ_cols;

      // velocity and accelaration finishing
      ov += vJ;
      oa += (ov ^ vJ) + data.oMi[i].act( jdata.S() * jmodel.jointVelocitySelector(a) + jdata.c() );

      // Composite rigid body inertia
      Inertia & oY =  data.oYcrb[i] ;

      oY = data.oMi[i].act(model.inertias[i]);
      data.of[i] = oY*oa + oY.vxiv(ov);
      data.oBcrb[i] = Coriolis(oY, ov );
    }
  };
  
  template<typename Scalar, int Options, template<typename,int> class JointCollectionTpl, typename MatrixType1, typename MatrixType2, typename MatrixType3>
  struct ComputeRNEADerivativesFasterBackwardStep
  : public fusion::JointUnaryVisitorBase<ComputeRNEADerivativesFasterBackwardStep<Scalar,Options,JointCollectionTpl,MatrixType1,MatrixType2,MatrixType3> >
  {
    typedef ModelTpl<Scalar,Options,JointCollectionTpl> Model;
    typedef DataTpl<Scalar,Options,JointCollectionTpl> Data;
    
    typedef boost::fusion::vector<const Model &,
                                  Data &,
                                  const MatrixType1 &,
                                  const MatrixType2 &,
                                  const MatrixType3 &
                                  > ArgsType;
    
    template<typename JointModel>
    static void algo(const JointModelBase<JointModel> & jmodel,
                     const Model & model,
                     Data & data,
                     const Eigen::MatrixBase<MatrixType1> & rnea_partial_dq,
                     const Eigen::MatrixBase<MatrixType2> & rnea_partial_dv,
                     const Eigen::MatrixBase<MatrixType3> & rnea_partial_da)
    {
      typedef typename Model::JointIndex JointIndex;
      
      const JointIndex & i = jmodel.id();
      const JointIndex & parent = model.parents[i];

      typedef typename SizeDepType<JointModel::NV>::template ColsReturn<typename Data::Matrix6x>::Type ColsBlock;
      
      ColsBlock J_cols = jmodel.jointCols(data.J);
      ColsBlock dJ_cols = jmodel.jointCols(data.dJ);
      ColsBlock ddJ_cols = jmodel.jointCols(data.ddJ);
      ColsBlock vdJ_cols = jmodel.jointCols(data.vdJ);

      ColsBlock tmp1 = jmodel.jointCols(data.Ftmp1);
      ColsBlock tmp2 = jmodel.jointCols(data.Ftmp2);
      ColsBlock tmp3 = jmodel.jointCols(data.Ftmp3);
      ColsBlock tmp4 = jmodel.jointCols(data.Ftmp4);

      const Eigen::Index joint_idx  = (Eigen::Index) jmodel.idx_v();
      const Eigen::Index joint_dofs = (Eigen::Index) jmodel.nv();
      const Eigen::Index subtree_dofs = (Eigen::Index) data.nvSubtree[i];
      const Eigen::Index successor_idx = joint_idx + joint_dofs;
      const Eigen::Index successor_dofs = subtree_dofs -joint_dofs;

      Inertia & oYcrb = data.oYcrb[i];
      Coriolis& oBcrb = data.oBcrb[i];

      
      MatrixType1 & rnea_partial_dq_ = PINOCCHIO_EIGEN_CONST_CAST(MatrixType1,rnea_partial_dq);
      MatrixType2 & rnea_partial_dv_ = PINOCCHIO_EIGEN_CONST_CAST(MatrixType2,rnea_partial_dv);
      MatrixType3 & rnea_partial_da_ = PINOCCHIO_EIGEN_CONST_CAST(MatrixType3,rnea_partial_da);

      motionSet::inertiaAction(oYcrb,J_cols,tmp1);
      
      motionSet::coriolisAction(oBcrb,J_cols,tmp2);
      motionSet::inertiaAction<ADDTO>(oYcrb,vdJ_cols,tmp2);

      motionSet::coriolisAction(oBcrb,dJ_cols,tmp3);
      motionSet::inertiaAction<ADDTO>(oYcrb,ddJ_cols,tmp3);
      
      motionSet::act<ADDTO>(J_cols,data.of[i],tmp3);

      motionSet::coriolisTransposeAction(oBcrb,J_cols,tmp4);


      if( successor_dofs > 0 ) 
      {
      	rnea_partial_dq_.block( joint_idx, successor_idx, joint_dofs, successor_dofs ).noalias()
        	= J_cols.transpose()*data.Ftmp3.middleCols( successor_idx, successor_dofs );

        rnea_partial_dv_.block( joint_idx, successor_idx, joint_dofs, successor_dofs ).noalias()
        	= J_cols.transpose()*data.Ftmp2.middleCols( successor_idx, successor_dofs );
      }

      rnea_partial_dq_.block( joint_idx, joint_idx, subtree_dofs, joint_dofs ).noalias()
        =  data.Ftmp1.middleCols( joint_idx, subtree_dofs ).transpose()*ddJ_cols 
         + data.Ftmp4.middleCols( joint_idx, subtree_dofs ).transpose()*dJ_cols;

      rnea_partial_dv_.block( joint_idx, joint_idx, subtree_dofs, joint_dofs ).noalias()
        =   data.Ftmp1.middleCols( joint_idx, subtree_dofs ).transpose()*vdJ_cols
          + data.Ftmp4.middleCols( joint_idx, subtree_dofs ).transpose()*J_cols;
      
      rnea_partial_da_.block( joint_idx, joint_idx, joint_dofs, subtree_dofs ).noalias() =
        J_cols.transpose()*data.Ftmp1.middleCols( joint_idx, subtree_dofs );


      if(parent>0)
      {
        data.oYcrb[parent] += data.oYcrb[i];
        data.oBcrb[parent] += data.oBcrb[i];
        data.of[parent] += data.of[i];
      }
    }
  };
  
  template<typename Scalar, int Options, template<typename,int> class JointCollectionTpl, typename ConfigVectorType, typename TangentVectorType1, typename TangentVectorType2,
  typename MatrixType1, typename MatrixType2, typename MatrixType3>
  inline void
  computeRNEADerivativesFaster(const ModelTpl<Scalar,Options,JointCollectionTpl> & model,
                         DataTpl<Scalar,Options,JointCollectionTpl> & data,
                         const Eigen::MatrixBase<ConfigVectorType> & q,
                         const Eigen::MatrixBase<TangentVectorType1> & v,
                         const Eigen::MatrixBase<TangentVectorType2> & a,
                         const Eigen::MatrixBase<MatrixType1> & rnea_partial_dq,
                         const Eigen::MatrixBase<MatrixType2> & rnea_partial_dv,
                         const Eigen::MatrixBase<MatrixType3> & rnea_partial_da)
  {
    PINOCCHIO_CHECK_ARGUMENT_SIZE(q.size(), model.nq, "The joint configuration vector is not of right size");
    PINOCCHIO_CHECK_ARGUMENT_SIZE(v.size(), model.nv, "The joint velocity vector is not of right size");
    PINOCCHIO_CHECK_ARGUMENT_SIZE(a.size(), model.nv, "The joint acceleration vector is not of right size");
    PINOCCHIO_CHECK_ARGUMENT_SIZE(rnea_partial_dq.cols(), model.nv);
    PINOCCHIO_CHECK_ARGUMENT_SIZE(rnea_partial_dq.rows(), model.nv);
    PINOCCHIO_CHECK_ARGUMENT_SIZE(rnea_partial_dv.cols(), model.nv);
    PINOCCHIO_CHECK_ARGUMENT_SIZE(rnea_partial_dv.rows(), model.nv);
    PINOCCHIO_CHECK_ARGUMENT_SIZE(rnea_partial_da.cols(), model.nv);
    PINOCCHIO_CHECK_ARGUMENT_SIZE(rnea_partial_da.rows(), model.nv);
    assert(model.check(data) && "data is not consistent with model.");
    
    typedef ModelTpl<Scalar,Options,JointCollectionTpl> Model;
    typedef typename Model::JointIndex JointIndex;
    
    
    typedef ComputeRNEADerivativesFasterForwardStep<Scalar,Options,JointCollectionTpl,ConfigVectorType,TangentVectorType1,TangentVectorType2> Pass1;
    for(JointIndex i=1; i<(JointIndex) model.njoints; ++i)
    {
      Pass1::run(model.joints[i],data.joints[i],
                 typename Pass1::ArgsType(model,data,q.derived(),v.derived(),a.derived()));
    }
    
    typedef ComputeRNEADerivativesFasterBackwardStep<Scalar,Options,JointCollectionTpl,MatrixType1,MatrixType2,MatrixType3> Pass2;
    for(JointIndex i=(JointIndex)(model.njoints-1); i>0; --i)
    {
      Pass2::run(model.joints[i],
                 typename Pass2::ArgsType(model,data,
                                          PINOCCHIO_EIGEN_CONST_CAST(MatrixType1,rnea_partial_dq),
                                          PINOCCHIO_EIGEN_CONST_CAST(MatrixType2,rnea_partial_dv),
                                          PINOCCHIO_EIGEN_CONST_CAST(MatrixType3,rnea_partial_da)));
    }
  }
  
  template<typename Scalar, int Options, template<typename,int> class JointCollectionTpl, typename ConfigVectorType, typename TangentVectorType1, typename TangentVectorType2,
  typename MatrixType1, typename MatrixType2, typename MatrixType3>
  inline void
  computeRNEADerivativesFaster(const ModelTpl<Scalar,Options,JointCollectionTpl> & model,
                         DataTpl<Scalar,Options,JointCollectionTpl> & data,
                         const Eigen::MatrixBase<ConfigVectorType> & q,
                         const Eigen::MatrixBase<TangentVectorType1> & v,
                         const Eigen::MatrixBase<TangentVectorType2> & a,
                         const container::aligned_vector< ForceTpl<Scalar,Options> > & fext,
                         const Eigen::MatrixBase<MatrixType1> & rnea_partial_dq,
                         const Eigen::MatrixBase<MatrixType2> & rnea_partial_dv,
                         const Eigen::MatrixBase<MatrixType3> & rnea_partial_da)
  {
    PINOCCHIO_CHECK_ARGUMENT_SIZE(q.size(), model.nq, "The joint configuration vector is not of right size");
    PINOCCHIO_CHECK_ARGUMENT_SIZE(v.size(), model.nv, "The joint velocity vector is not of right size");
    PINOCCHIO_CHECK_ARGUMENT_SIZE(a.size(), model.nv, "The joint acceleration vector is not of right size");
    PINOCCHIO_CHECK_ARGUMENT_SIZE(fext.size(), (size_t)model.njoints, "The size of the external forces is not of right size");
    PINOCCHIO_CHECK_ARGUMENT_SIZE(rnea_partial_dq.cols(), model.nv);
    PINOCCHIO_CHECK_ARGUMENT_SIZE(rnea_partial_dq.rows(), model.nv);
    PINOCCHIO_CHECK_ARGUMENT_SIZE(rnea_partial_dv.cols(), model.nv);
    PINOCCHIO_CHECK_ARGUMENT_SIZE(rnea_partial_dv.rows(), model.nv);
    PINOCCHIO_CHECK_ARGUMENT_SIZE(rnea_partial_da.cols(), model.nv);
    PINOCCHIO_CHECK_ARGUMENT_SIZE(rnea_partial_da.rows(), model.nv);
    assert(model.check(data) && "data is not consistent with model.");
    
    typedef ModelTpl<Scalar,Options,JointCollectionTpl> Model;
    typedef typename Model::JointIndex JointIndex;
    
    
    typedef ComputeRNEADerivativesFasterForwardStep<Scalar,Options,JointCollectionTpl,ConfigVectorType,TangentVectorType1,TangentVectorType2> Pass1;
    for(JointIndex i=1; i<(JointIndex) model.njoints; ++i)
    {
      Pass1::run(model.joints[i],data.joints[i],
                 typename Pass1::ArgsType(model,data,q.derived(),v.derived(),a.derived()));
      data.of[i] -= data.oMi[i].act(fext[i]);
    }
    
    typedef ComputeRNEADerivativesFasterBackwardStep<Scalar,Options,JointCollectionTpl,MatrixType1,MatrixType2,MatrixType3> Pass2;
    for(JointIndex i=(JointIndex)(model.njoints-1); i>0; --i)
    {
      Pass2::run(model.joints[i],
                 typename Pass2::ArgsType(model,data,
                                          PINOCCHIO_EIGEN_CONST_CAST(MatrixType1,rnea_partial_dq),
                                          PINOCCHIO_EIGEN_CONST_CAST(MatrixType2,rnea_partial_dv),
                                          PINOCCHIO_EIGEN_CONST_CAST(MatrixType3,rnea_partial_da)));
    }
  }
  

} // namespace pinocchio


#endif // ifndef __pinocchio_rnea_derivatives_hxx__
