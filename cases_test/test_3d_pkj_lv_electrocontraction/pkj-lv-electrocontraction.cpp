/**
 * @file 	excitation-contraction for left ventricle heart model.cpp
 * @brief 	This is the case studying the electromechanics on a biventricular heart model in 3D.
 * @author 	Chi Zhang and Xiangyu Hu
 * @version 0.2.1
 * 			Chi Zhang
 * 			Unit :
 *			time t = ms = 12.9 [-]
 * 			length l = mm
 * 			mass m = g
 *			density rho = g * (mm)^(-3)
 *			Pressure pa = g * (mm)^(-1) * (ms)^(-2)
 *			diffusion d = (mm)^(2) * (ms)^(-2)
 *@version 0.3
 *			Here, the coupling with Purkinje network will be condcuted.
 */
/**  SPHinXsys Library. */
#include "sphinxsys.h"
#include "case.h"
/** Namespace cite here. */
using namespace SPH;
/** 
 * The main program. 
 */
int main(int ac, char* av[])
{
	/** 
	 * Build up context -- a SPHSystem. 
	 */
	SPHSystem system(system_domain_bounds, dp_0);
	/** Set the starting time. */
	GlobalStaticVariables::physical_time_ = 0.0;
	/** Tag for run particle relaxation for the initial body fitted distribution. */
	system.run_particle_relaxation_ = false;
	/** Tag for reload initially repaxed particles. */
	system.reload_particles_ = true;
	/** Tag for computation from restart files. 0: not from restart files. */
	system.restart_step_ = 0;
	//handle command line arguments
	#ifdef BOOST_AVAILABLE
	system.handleCommandlineOptions(ac, av);
	#endif
	/** in- and output environment. */
	In_Output 	in_output(system);
	
	/** Creat a heart body for physiology, material and particles */
	HeartBody* physiology_body = new HeartBody(system, "ExcitationHeart");
	if (!system.run_particle_relaxation_ && system.reload_particles_) physiology_body->useParticleGeneratorReload();
	MuscleReactionModel* muscle_reaction_model = new MuscleReactionModel();
	MyocardiumPhysiology* myocardium_excitation = new MyocardiumPhysiology(muscle_reaction_model);
	ElectroPhysiologyParticles 	physiology_articles(physiology_body, myocardium_excitation);
	
	/** Creat a heart body for excitation-contraction, material and particles */
	HeartBody* mechanics_body = new HeartBody(system, "ContractionHeart");
	if (!system.run_particle_relaxation_ && system.reload_particles_) mechanics_body->useParticleGeneratorReload();
	MyocardiumMuscle* myocardium_muscle = new MyocardiumMuscle();
	ActiveMuscleParticles 	mechanics_particles(mechanics_body, myocardium_muscle);
	
	/** check whether reload material properties. */
	if (!system.run_particle_relaxation_ && system.reload_particles_)
	{
		std::unique_ptr<ReloadMaterialParameterIO>
			read_muscle_fiber_and_sheet(new ReloadMaterialParameterIO(in_output, myocardium_muscle));
		std::unique_ptr<ReloadMaterialParameterIO>
			read_myocardium_excitation_fiber(new ReloadMaterialParameterIO(in_output, myocardium_excitation, myocardium_muscle->LocalParametersName()));
		read_muscle_fiber_and_sheet->ReadFromFile();
		read_myocardium_excitation_fiber->ReadFromFile();
	}
	
	/** Creat a Purkinje network for fast diffusion, material and particles */
	PurkinjeBody *pkj_body = new PurkinjeBody(system, "Purkinje", new NetworkGeneratorwihtExtraCheck(starting_point, second_point, 50, 1.0));
	MuscleReactionModel *pkj_reaction_model = new MuscleReactionModel();
	FastMyocardiumMuscle 	*pkj_myocardium_muscle = new FastMyocardiumMuscle(pkj_reaction_model);
	ElectroPhysiologyReducedParticles 		pkj_muscle_particles(pkj_body, pkj_myocardium_muscle);
	TreeLeaves *pkj_leaves = new TreeLeaves(pkj_body);
	/** check whether run particle relaxation for body fitted particle distribution. */
	if (system.run_particle_relaxation_)
	{
		HeartBody* relax_body = new HeartBody(system, "RelaxationHeart");
		DiffusionMaterial* relax_body_material = new DiffusionMaterial();
		DiffusionReactionParticles<ElasticSolidParticles, LocallyOrthotropicMuscle>	diffusion_particles(relax_body, relax_body_material);
		/** topology */
		InnerBodyRelation* relax_body_inner = new InnerBodyRelation(relax_body);
		/** Random reset the relax solid particle position. */
		RandomizePartilePosition  			random_particles(relax_body);
		/**Algorithms for particle relaxation.*/
		 /** A  Physics relaxation step. */
		relax_dynamics::RelaxationStepInner relaxation_step_inner(relax_body_inner);
		/** Diffusion process.*/
		 /** Time step for diffusion. */
		GetDiffusionTimeStepSize<SolidBody, ElasticSolidParticles, LocallyOrthotropicMuscle> get_time_step_size(relax_body);
		/** Diffusion process for diffusion body. */
		DiffusionRelaxation 			diffusion_relaxation(relax_body_inner);
		/** Compute the fiber and sheet after diffusion. */
		ComputeFiberandSheetDirections compute_fiber_sheet(relax_body);
		/** Write the body state to Vtu file. */
		WriteBodyStatesToPlt 		write_relax_body_state(in_output, { relax_body });
		/** Write the particle reload files. */
		ReloadParticleIO 		
			write_particle_reload_files(in_output, { relax_body, relax_body }, 
			{ physiology_body->getBodyName(), mechanics_body->getBodyName()});
		/** Write material property to xml file. */
		ReloadMaterialParameterIO 
			write_material_property(in_output, relax_body_material, myocardium_muscle->LocalParametersName());
		/**Physics relaxation starts here.*/
		 /** Relax the elastic structure. */
		random_particles.parallel_exec(0.25);
		relaxation_step_inner.surface_bounding_.parallel_exec();
		write_relax_body_state.WriteToFile(0.0);
		/**
		 * From here the time stepping begines.
		 * Set the starting time.
		 */
		int ite = 0;
		int relax_step = 1000;
		int diffusion_step = 100;
		while (ite < relax_step)
		{
			relaxation_step_inner.parallel_exec();
			ite++;
			if (ite % 100 == 0)
			{
				cout << fixed << setprecision(9) << "Relaxation steps N = " << ite << "\n";
				write_relax_body_state.WriteToFile(Real(ite) * 1.0e-4);
			}
		}
		ShapeSurface* surface_part = new ShapeSurface(relax_body);
		/** constraint boundary condition for diffusion. */
		DiffusionBCs impose_diffusion_bc(relax_body, surface_part);
		impose_diffusion_bc.parallel_exec();
		write_relax_body_state.WriteToFile(Real(ite) * 1.0e-4);
		Real dt = get_time_step_size.parallel_exec();
		while (ite <= diffusion_step + relax_step)
		{
			diffusion_relaxation.parallel_exec(dt);
			impose_diffusion_bc.parallel_exec();
			if (ite % 10 == 0)
			{
				cout << "Diffusion steps N=" << ite - relax_step << "	dt: " << dt << "\n";
				write_relax_body_state.WriteToFile(Real(ite) * 1.0e-4);
			}
			ite++;
		}
		compute_fiber_sheet.exec();
		ite++;
		write_relax_body_state.WriteToFile(Real(ite) * 1.0e-4);
		compute_fiber_sheet.parallel_exec();
		write_material_property.WriteToFile(0);
		write_particle_reload_files.WriteToFile(0);

		return 0;
	}
	/**
	 * Particle and body creation of fluid observer.
	 */
	VoltageObserver* voltage_observer = new VoltageObserver(system, "VoltageObserver");
	BaseParticles 		observer_particles(voltage_observer);
	/** Define muscle Observer. */
	MyocardiumObserver* myocardium_observer	= new MyocardiumObserver(system, "MyocardiumObserver");
	BaseParticles 	disp_observer_particles(myocardium_observer);

	WriteBodyStatesToPlt 		write_states(in_output, system.real_bodies_);
	/** topology */
	InnerBodyRelation* physiology_body_inner = new InnerBodyRelation(physiology_body);	
	InnerBodyRelation* mechanics_body_inner = new InnerBodyRelation(mechanics_body);
	ContactBodyRelation* physiology_body_contact = new ContactBodyRelation(physiology_body, { mechanics_body });
	ContactBodyRelation* mechanics_body_contact = new ContactBodyRelation(mechanics_body, { physiology_body });
	ContactBodyRelation* voltage_observer_contact = new ContactBodyRelation(voltage_observer, { physiology_body });	
	ContactBodyRelation* myocardium_observer_contact = new ContactBodyRelation(myocardium_observer, { mechanics_body });
	ComplexBodyRelation* physiology_body_complex = new ComplexBodyRelation(physiology_body, {pkj_leaves});
	ReducedInnerBodyRelation* pkj_inner = new ReducedInnerBodyRelation(pkj_body);

	/** Corrected strong configuration. */	
	solid_dynamics::CorrectConfiguration correct_configuration_excitation(physiology_body_inner);
	/** Time step size calculation. */
	electro_physiology::GetElectroPhysiologyTimeStepSize get_myocardium_physiology_time_step(physiology_body);
	/** Diffusion process for diffusion body. */
	electro_physiology::ElectroPhysiologyDiffusionRelaxationComplex myocardium_diffusion_relaxation(physiology_body_complex);
	/** Solvers for ODE system */
	electro_physiology::ElectroPhysiologyReactionRelaxationForward 	myocardium_reaction_relaxation_forward(physiology_body);
	electro_physiology::ElectroPhysiologyReactionRelaxationBackward myocardium_reaction_relaxation_backward(physiology_body);
	/** Physiology for PKJ*/
	/** Time step size calculation. */
	electro_physiology::GetElectroPhysiologyTimeStepSize 				get_pkj_physiology_time_step(pkj_body);
	electro_physiology::ElectroPhysiologyDiffusionRelaxationInner 		pkj_diffusion_relaxation(pkj_inner);
	/** Solvers for ODE system */
	electro_physiology::ElectroPhysiologyReactionRelaxationForward 		pkj_reaction_relaxation_forward(pkj_body);
	electro_physiology::ElectroPhysiologyReactionRelaxationBackward 	pkj_reaction_relaxation_backward(pkj_body);
	/**IO for observer.*/
	WriteAnObservedQuantity<indexScalar, Real> write_voltage("Voltage", in_output, voltage_observer_contact);
	WriteAnObservedQuantity<indexVector, Vecd> write_displacement("Position", in_output, myocardium_observer_contact);
	/**Apply the Iron stimulus.*/
	ApplyStimulusCurrentToMmyocardium	apply_stimulus_myocardium(physiology_body);
	ApplyStimulusCurrentToPKJ			apply_stimulus_pkj(pkj_body);
	/** Active mechanics. */
	solid_dynamics::CorrectConfiguration correct_configuration_contraction(mechanics_body_inner);
	/** Observer Dynamics */
	observer_dynamics::CorrectInterpolationKernelWeights
		correct_kernel_weights_for_interpolation(mechanics_body_contact);
	/** Interpolate the active contract stress from eletrophyisology body. */
	observer_dynamics::InterpolatingAQuantity<indexScalar, Real>
		active_stress_interpolation(mechanics_body_contact, "ActiveContractionStress");
	/** Interpolate the particle position in physiology_body  from mechanics_body. */
	observer_dynamics::InterpolatingAQuantity<indexVector, Vecd>
		interpolation_particle_position(physiology_body_contact, "Position", "Position");
	/** Time step size calculation. */
	solid_dynamics::AcousticTimeStepSize 		get_mechanics_time_step(mechanics_body);
	/** active and passive stress relaxation. */
	solid_dynamics::StressRelaxationFirstHalf 	stress_relaxation_first_half(mechanics_body_inner);
	solid_dynamics::StressRelaxationSecondHalf 	stress_relaxation_second_half(mechanics_body_inner);
	/** Constrain region of the inserted body. */
	solid_dynamics::ConstrainSolidBodyRegion 	constrain_holder(mechanics_body, new MuscleBase(mechanics_body, "Holder"));
	/** 
	 * Pre-simultion. 
	 */
	system.initializeSystemCellLinkedLists();
	system.initializeSystemConfigurations();
	correct_configuration_excitation.parallel_exec();
	correct_configuration_contraction.parallel_exec();
	correct_kernel_weights_for_interpolation.parallel_exec();
	/**Output global basic parameters. */
	write_states.WriteToFile(GlobalStaticVariables::physical_time_);
	write_voltage.WriteToFile(GlobalStaticVariables::physical_time_);
	write_displacement.WriteToFile(GlobalStaticVariables::physical_time_);
	write_states.WriteToFile(GlobalStaticVariables::physical_time_);
	/**
	 * main loop. 
	 */
	int screen_output_interval 	= 10;
	int ite 					= 0;
	int reaction_step 			= 2;
	Real End_Time 				= 80;
	Real Ouput_T 				= End_Time / 200.0;
	Real Observer_time 			= 0.01 * Ouput_T;	
	Real dt_myocardium 			= 0.0;
	Real dt_pkj 				= 0.0;  
	Real dt_muscle 				= 0.0;
	/** Statistics for computing time. */
	tick_count t1 = tick_count::now();
	tick_count::interval_t interval;
	cout << "Main Loop Starts Here : " << "\n";
	/** Main loop starts here. */ 
	while (GlobalStaticVariables::physical_time_ < End_Time)
	{
		Real integration_time = 0.0;
		while (integration_time < Ouput_T) 
		{
			Real relaxation_time = 0.0;
			while (relaxation_time < Observer_time) 
			{
				if (ite % screen_output_interval == 0) 
				{
					cout << fixed << setprecision(9) << "N=" << ite << "	Time = "
						<< GlobalStaticVariables::physical_time_
						<< "	dt_pkj = " << dt_pkj 
						<< "	dt_myocardium = " << dt_myocardium 
						<< "	dt_muscle = " << dt_muscle << "\n";
				}
				/** Apply stimulus excitation. */
				// if( 0 <= GlobalStaticVariables::physical_time_ 
				// 	&&  GlobalStaticVariables::physical_time_ <= 0.5)
				// {
				// 	apply_stimulus_myocardium.parallel_exec(dt_myocardium);
				// }

				Real dt_pkj_sum = 0.0;
				while (dt_pkj_sum < dt_myocardium) 
				{
					/**
					 * When network generates particles, the final particle spacing, which is after particle projected in to 
					 * complex geometry, may small than the reference one, therefore, a smaller time step size is required. 
					 */
					dt_pkj = 0.5 * get_pkj_physiology_time_step.parallel_exec();
					if (dt_myocardium - dt_pkj_sum < dt_pkj) dt_pkj = dt_myocardium - dt_pkj_sum;

					if( 0 <= GlobalStaticVariables::physical_time_ 
					&&  GlobalStaticVariables::physical_time_ <= 0.5)
					{
						apply_stimulus_pkj.parallel_exec(dt_pkj);
					}
					/**Strang splitting method. */
					int ite_pkj_forward = 0;
					while (ite_pkj_forward < reaction_step )
					{
						pkj_reaction_relaxation_forward.parallel_exec(0.5 * dt_pkj / Real(reaction_step));
						ite_pkj_forward ++;
					}
					/** 2nd Runge-Kutta scheme for diffusion. */
					pkj_diffusion_relaxation.parallel_exec(dt_pkj);
					//backward reaction
					int ite_pkj_backward = 0;
					while (ite_pkj_backward < reaction_step)
					{
						pkj_reaction_relaxation_backward.parallel_exec(0.5 * dt_pkj / Real(reaction_step));
						ite_pkj_backward ++;
					}

					dt_pkj_sum += dt_pkj;
				}

				/**Strang splitting method. */
				int ite_forward = 0;
				while (ite_forward < reaction_step )
				{
					myocardium_reaction_relaxation_forward.parallel_exec(0.5 * dt_myocardium / Real(reaction_step));
					ite_forward ++;
				}
				/** 2nd Runge-Kutta scheme for diffusion. */
				myocardium_diffusion_relaxation.parallel_exec(dt_myocardium);

				//backward reaction
				int ite_backward = 0;
				while (ite_backward < reaction_step)
				{
					myocardium_reaction_relaxation_backward.parallel_exec(0.5 * dt_myocardium / Real(reaction_step));
					ite_backward ++;
				}

				active_stress_interpolation.parallel_exec();
				Real dt_muscle_sum = 0.0;
				while (dt_muscle_sum < dt_myocardium) 
				{
					dt_muscle = get_mechanics_time_step.parallel_exec();
					if (dt_myocardium - dt_muscle_sum < dt_muscle) dt_muscle = dt_myocardium - dt_muscle_sum;
					stress_relaxation_first_half.parallel_exec(dt_muscle);
					constrain_holder.parallel_exec(dt_muscle);
					stress_relaxation_second_half.parallel_exec(dt_muscle);
					dt_muscle_sum += dt_muscle;
				}

				ite++;
				dt_myocardium = get_myocardium_physiology_time_step.parallel_exec();

				relaxation_time += dt_myocardium;
				integration_time += dt_myocardium;
				GlobalStaticVariables::physical_time_ += dt_myocardium;
			}
			write_voltage.WriteToFile(GlobalStaticVariables::physical_time_);
			write_displacement.WriteToFile(GlobalStaticVariables::physical_time_);
		}
		tick_count t2 = tick_count::now();
		interpolation_particle_position.parallel_exec();
		write_states.WriteToFile(GlobalStaticVariables::physical_time_);
		tick_count t3 = tick_count::now();
		interval += t3 - t2;
	}
	tick_count t4 = tick_count::now();

	tick_count::interval_t tt;
	tt = t4 - t1 - interval;
	cout << "Total wall time for computation: " << tt.seconds() << " seconds." << endl;

	return 0;
}