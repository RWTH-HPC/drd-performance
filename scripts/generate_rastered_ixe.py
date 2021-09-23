import matplotlib as mpl
mpl.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.backends.backend_pdf as plt_backend
import matplotlib.ticker as mticker
import numpy as np
import csv
import subprocess
import sys


#########################
#    CONFIGURATIONS     #
#########################

use_file_and_line = False
path="../data/archer-tsan"

TINY_SIZE = 5
SMALL_SIZE = 6
MEDIUM_SIZE = 6
LARGE_SIZE = 7

MARKER_SIZE = 5
MARKER_WIDTH = 1


#########################
#   CLASS DEFINITIONS   #
#########################


###
# Class to manage all data of a complete measurement run (multiple tnums, multiple applications)
#  - stores all meta data
#  - stores all data in Application objects dict
###
class Measurement():
  def __init__(self,counters,tnums,variants,meas_states,output_format,applications):
    self.counters = np.array(counters)
    self.tnums = np.array(tnums)
    self.variants = np.array(variants)
    self.meas_states = np.array(meas_states)
    self.output_format = output_format
    self.applications = {}
    
    for a_name in applications:
      self.applications[a_name] = Application(a_name,len(counters),len(tnums),len(variants),len(meas_states))
  
  # draw all possible diagrams
  def draw_all(self,output_path):
    # self.draw_meas_overhead(output_path)
    self.draw_variant_overhead(output_path)
    self.draw_region_time_overheads(output_path)
    self.draw_region_counter_overheads(output_path)
  
    
  # store data from one line from the output_all csv
  # use_extended_name determines if only the region name is used or if filename+codeline+regiontype is used to determine different regions
  def add_region_line(self, line, use_extended_name):
    application = line[0]
    tnum_index = self.tnum_to_index(line[1])
    variant_index = self.variant_to_index(line[2])
    if (use_extended_name):
      region_type = ''.join(filter(lambda c : not str.isdigit(c),line[6]))
      if (line[6][:5] == "user_"):
        region_type = "UserRegion"
      region_name = line[4].split("/")[-1]+"-"+line[5]+"-"+region_type
    else:
      region_name = line[6]
    if (self.output_format == "flat"):
      ctr_values = line[7:]
    else:
      region_name = str(line[7]) + " " + region_name
      ctr_values = line[8:]
    
    self.applications[application].add_region_value(region_name,tnum_index,variant_index,ctr_values)
  
  
  # draw diagrams per application and variant and counter with the region overheads
  def draw_region_overheads(self,output_path,excl_or_incl,apps="all"):
    width = 0.3
    meas_idx = self.meas_state_to_index("profiler")
  
    print("Printing region overheads ...", end=' ', flush=True)

    for a in self.applications.values():
      if (apps=="all" or a.name in apps):
        print(a.name, end=' ', flush=True)
        with plt_backend.PdfPages(output_path+"/output_"+excl_or_incl+"_"+a.name+"_region_overhead.pdf") as pdf:
          for c in self.counters:
            ctr_idx = self.ctr_to_index(c)
            for variant in self.variants:
              if (variant != "base"):
                title = "Comparing "+a.name+"-"+variant+" variant with base\nfor metric "+c
                variant_idx = self.variant_to_index(variant)
                base_idx = self.variant_to_index("base")
                a.draw_region_overheads(pdf,title,width,self.tnums,variant_idx,base_idx,ctr_idx,meas_idx,excl_or_incl)
          #pdf.savefig()
            
    print("Region overheads printed.")
  
  # draw diagrams per application and variant and region with the counter overheads  
  def draw_counter_overheads(self,output_path,excl_or_incl,apps="all"):
    width = 0.3
    meas_idx = self.meas_state_to_index("profiler")
    
    print("Printing counter overheads ...", end=' ', flush=True)

    for a in self.applications.values():
      if (apps=="all" or a.name in apps):
        print(a.name, end=' ', flush=True)
        with plt_backend.PdfPages(output_path+"/output_"+str(excl_or_incl)+"_"+a.name+"_counter_overhead.pdf") as pdf:
          for r_name in a.regions.keys():
            for variant in self.variants:
              title = a.name+"-"+variant
              variant_idx = self.variant_to_index(variant)
              base_idx = self.variant_to_index("base")
              if (variant != "base"):
                a.draw_counter_overheads(pdf,title,self.counters,width,self.tnums,variant_idx,base_idx,meas_idx,r_name,excl_or_incl)
          #pdf.savefig()
    print("Counter overheads printed.")
            
  # draw diagrams per application and variant and region with the counter overheads  
  def draw_cache_usage(self,output_path,excl_or_incl,apps="all"):
    width = 0.3
    meas_idx = self.meas_state_to_index("profiler")
    
    print("Printing cache usage ...", end=' ', flush=True)
    mapping=(3,1,2,4)
    for a in self.applications.values():
      if (apps=="all" or a.name in apps):
        print(a.name, end=' ', flush=True)
        with plt_backend.PdfPages(output_path+"/output_"+str(excl_or_incl)+"_"+a.name+"_cache_usage.pdf", keep_empty=False) as pdf:
          self.draw_app_region_times(pdf, excl_or_incl, a.name)
          for r_name in a.regions.keys():
            r = a.regions[r_name]
            fig_pie, axes2 = plt.subplots(2,3,subplot_kw=dict(polar=True), figsize=(8,5)) # polar plot in background to draw axes onto
            fig_pie.suptitle("Cache usage for region "+r_name+" of "+a.name, fontsize=LARGE_SIZE+2)
            axes2 = axes2.flatten()
#            fig_pie.set_title("Cache usage for region\n"+r_name,pad=30.0)
            

            for n,variant in enumerate(self.variants):
              # title = a.name+"-"+variant
              ax2 = axes2[mapping[n]]
              ax = fig_pie.add_subplot(*ax2.get_geometry(),polar=False) # non-polar plot in foreground for actual ring diagram
              #title = a.name.capitalize()+"-"+variant
              variant_idx = self.variant_to_index(variant)
              base_idx = self.variant_to_index("base")
              a.draw_cache_usages_axes(ax, ax2, variant,self.counters,width,self.tnums,variant_idx,base_idx,meas_idx,r_name,excl_or_incl)
#            fig_pie.legend(labels=labels_pie,loc='upper center',bbox_to_anchor=(0.5,-0.2),ncol=2)
            lines = ax.get_children()[:4]
            fig_pie.delaxes(axes2[0])
            fig_pie.delaxes(axes2[5])
            axl = fig_pie.add_subplot(231,frameon=False)
            axl.set_xticks([])
            axl.set_yticks([])
#            axl.visible=False
#            ax = axes2[0]
            labels_pie = ["L1D_HIT","L2D_HIT","L3_HIT","MEM"]
#            ax.legend(labels=labels_pie, loc="upper left", mode="expand", bbox_to_anchor=(0,-1,1,-1))
#            lines, labels = ax.get_legend_handles_labels()
#            print(len(ax.get_children()[:4]))
            axl.legend(lines, labels_pie, loc = 'center')
            fig_pie.tight_layout()
        
            pdf.savefig()
            plt.close()
#            break
    print("Cache usage printed.")
    
      
  # draw diagrams per application and variant and counter with the region overheads
  def draw_app_region_times(self,pdf,excl_or_incl,app):
    width = 0.3
    meas_idx = self.meas_state_to_index("profiler")
  
    print("Printing region times ...", end=' ', flush=True)

    a = self.applications[app]
    print(a.name, end=' ', flush=True)
    ctr_idx = self.ctr_to_index("Time")
    fig, axes = plt.subplots(len(self.variants),1,figsize=(8,8))
    fig.suptitle("Time per region for "+a.name, fontsize=LARGE_SIZE+2)
    for n,variant in enumerate(self.variants):
      variant_idx = self.variant_to_index(variant)
      a.draw_region_times(fig, axes[n],width,self.tnums, variant,variant_idx,ctr_idx,meas_idx,excl_or_incl)
    #print (axes[0].get_legend_handles_labels())
    axes[0].get_legend().set_visible(True)
#    fig.legend(*axes[0].get_legend_handles_labels(), bbox_to_anchor=(0,1,1,1), loc="lower left", mode="expand")
    axes[-1].set_xticklabels(a.regions.keys(), rotation=20, ha="right")
    pdf.savefig()
    plt.close()
    print("Region times printed.")
  
      
  
  
  # functions to turn the meta data names into the corresponding indices used in the underlying data structure
  #
  def tnum_to_index(self,tnum):
    return np.where(self.tnums == tnum)[0][0]
    
  def variant_to_index(self,variant):
    return np.where(self.variants == variant)[0][0]
    
  def meas_state_to_index(self,meas_state):
    return np.where(self.meas_states == meas_state)[0][0]
    
  def ctr_to_index(self,ctr):
    return np.where(self.counters == ctr)[0][0]
  
  # debug function to print the current data structure
  def print_data(self):
    print(self.counters)
    print(self.tnums)
    print(self.variants)
    print(self.meas_states)
    
    for b in self.applications.values():
      b.print_data()
      
      
      
###      
#  class to store all data corresponding to one application in one measurement run    
#   - stores application time data in DataSet
#   - store region data in Region objects dict
###   
class Application():
  def __init__(self,name,num_ctr,num_tnum,num_variants,num_meas_states):
    self.name = name
    self.region_size = [num_ctr,num_tnum,num_variants] # size of a region DataSet array, size=(#ctrs,#tnums,#variants)
    self.regions = {}
    
  #
  # add values
  #
    
  def add_region_value(self,region_name,tnum,variant,ctr_vals): #tnum and variant already have to be the corresponding indices of the DataSet
    # create new Region if not existing so far
    if region_name not in self.regions:
      self.regions[region_name] = Region(region_name,self.region_size)
    # add all corresponding ctr values
    self.regions[region_name].add_ctr_values(tnum,variant,ctr_vals)
  

  
  #
  # draw values
  #
  def draw_region_times(self,fig, ax,width,tnums, variant,variant_idx,ctr_idx,meas_idx,excl_or_incl):
    labels = self.regions.keys()
    cmap = plt.get_cmap("Blues")
    
    # read values and calculate overheads
    values_variant = np.zeros((len(labels),len(tnums)))
    
    for idx,r_name in enumerate(self.regions.keys()):
      r = self.regions[r_name]
      if(excl_or_incl == "excl"):
        values_variant[idx] = r.get_excl_variant_values(variant_idx)[ctr_idx,:]
      elif(excl_or_incl == "incl"):
        values_variant[idx] = r.get_incl_variant_values(variant_idx)[ctr_idx,:]
    
    usable_values = np.any((values_variant > 0),axis=1)
    if(np.any(usable_values)):
    
      # plot values
      #ax2 = ax.twinx()
      #ax2_label_variant = "variant"
      #ax2_label_base = "base"
      
   
      # cut off bars
      cutoff = calculate_cutoff(values_variant)
      nanmax = np.nanmax(np.array(values_variant).flatten())
      if (cutoff < nanmax):
        ax.set_ylim([0,cutoff])
      
      values_variant = values_variant[usable_values,:]

      labels = np.array(list(labels))
      labels = labels[usable_values]
      
      x = np.arange(0,labels.shape[0])*(len(tnums)+1)*width
      for tnum_idx,tnum in enumerate(tnums):
        x_offset = (tnum_idx+0.5-(0.5*len(tnums)))*width
        bars = ax.bar(x + x_offset, values_variant[:,tnum_idx], width, label=tnum, color=[cmap(0.9*(tnum_idx+1) / len(tnums))])
        # add cutoff flags
        if (cutoff < nanmax):
          add_cutoff_flags(ax,bars,cutoff,len(tnums),tnum_idx)
              
#        ax2.plot(x + x_offset, values_variant[:,tnum_idx],label=ax2_label_variant, color="black",marker="x",linestyle="none",markersize=MARKER_SIZE,markeredgewidth=MARKER_WIDTH)
#        ax2.plot(x + x_offset, values_base[:,tnum_idx],label=ax2_label_base, color="black",marker="+",linestyle="none",markersize=MARKER_SIZE,markeredgewidth=MARKER_WIDTH)
        #if-cases to only show the explanation of symbols once in the legend
#        if (ax2_label_variant == "variant"):
#          ax2_label_variant = None
#        if (ax2_label_base == "base"):
#          ax2_label_base = None
      
      # Overhead axis properties    
      ax.set_ylabel("Execution time [s]")
      ax.set_title(variant, fontsize=LARGE_SIZE)
      ax.set_xticks(x)
      #ax.set_xticklabels(labels, rotation=20, ha="right", visible=False)
      ax.set_xticklabels([])
      leg = ax.legend(loc='lower right', bbox_to_anchor=(1, 1),ncol=len(values_variant[0,:]), handlelength=0.8)
      leg.set_visible(False)
      # Absolute values axis properties 
#      ax2.set_ylabel("Absolute values")
#      ax2.set_yscale("log")
#      ax2.autoscale(axis="y")
#      ax2.legend(loc='lower right', bbox_to_anchor=(1, 1),ncol=2, handlelength=0.8)
      
  
  def draw_region_overheads(self,pdf,title,width,tnums,variant_idx,base_idx,ctr_idx,meas_idx,excl_or_incl):
    labels = self.regions.keys()
    cmap = plt.get_cmap("Blues")
    
    # read values and calculate overheads
    overheads = np.zeros((len(labels),len(tnums)))
    values_base = np.zeros((len(labels),len(tnums)))
    values_variant = np.zeros((len(labels),len(tnums)))
    
    for idx,r_name in enumerate(self.regions.keys()):
      r = self.regions[r_name]
      if(excl_or_incl == "excl"):
        overheads[idx] = r.get_excl_variant_overhead(variant_idx,base_idx)[ctr_idx,:]
        values_base[idx] = r.get_excl_variant_values(base_idx)[ctr_idx,:]
        values_variant[idx] = r.get_excl_variant_values(variant_idx)[ctr_idx,:]
      elif(excl_or_incl == "incl"):
        overheads[idx] = r.get_incl_variant_overhead(variant_idx,base_idx)[ctr_idx,:]
        values_base[idx] = r.get_incl_variant_values(base_idx)[ctr_idx,:]
        values_variant[idx] = r.get_incl_variant_values(variant_idx)[ctr_idx,:]
    
    usable_values = np.any((values_base > 0),axis=1)
    if(np.any(usable_values)):
    
      # plot values
      fig, ax = plt.subplots()
      ax2 = ax.twinx()
      ax2_label_variant = "variant"
      ax2_label_base = "base"
      
   
      # cut off bars
      cutoff = calculate_cutoff(overheads)
      nanmax = np.nanmax(np.array(overheads).flatten())
      if (cutoff < nanmax):
        ax.set_ylim([0,cutoff])
      
      
      overheads = overheads[usable_values,:]
      values_variant = values_variant[usable_values,:]
      values_base = values_base[usable_values,:]

      labels = np.array(list(labels))
      labels = labels[usable_values]
      
      x = np.arange(0,labels.shape[0])*(len(tnums)+1)*width
      for tnum_idx,tnum in enumerate(tnums):
        x_offset = (tnum_idx+0.5-(0.5*len(tnums)))*width
        bars = ax.bar(x + x_offset, overheads[:,tnum_idx], width, label=tnum, color=[cmap(0.9*(tnum_idx+1) / len(tnums))])
        # add cutoff flags
        if (cutoff < nanmax):
          add_cutoff_flags(ax,bars,cutoff,len(tnums),tnum_idx)
              
        ax2.plot(x + x_offset, values_variant[:,tnum_idx],label=ax2_label_variant, color="black",marker="x",linestyle="none",markersize=MARKER_SIZE,markeredgewidth=MARKER_WIDTH)
        ax2.plot(x + x_offset, values_base[:,tnum_idx],label=ax2_label_base, color="black",marker="+",linestyle="none",markersize=MARKER_SIZE,markeredgewidth=MARKER_WIDTH)
        #if-cases to only show the explanation of symbols once in the legend
        if (ax2_label_variant == "variant"):
          ax2_label_variant = None
        if (ax2_label_base == "base"):
          ax2_label_base = None
      
      # Overhead axis properties    
      ax.set_ylabel("Relative value")
      ax.set_title("B. "+title, pad=30.0, fontdict={'fontweight' : 'bold','fontsize' : 18})
      ax.set_xticks(x)
      ax.set_xticklabels(labels, rotation=20, ha="right")
      ax.legend(loc='lower left', bbox_to_anchor=(0, 1),ncol=len(overheads[0,:]), handlelength=0.8)
      
      # Absolute values axis properties 
      ax2.set_ylabel("Absolute values")
      ax2.set_yscale("log")
      ax2.autoscale(axis="y")
      ax2.legend(loc='lower right', bbox_to_anchor=(1, 1),ncol=2, handlelength=0.8)
      
      fig.tight_layout()
      
      pdf.savefig()
      plt.close()
    
  def draw_cache_usages(self,pdf,title,counters,width,tnums,variant_idx,base_idx,meas_idx,r_name,excl_or_incl):
    cmap = plt.get_cmap("Blues")
    # check if ring diagrams should be drawn
    if np.all(np.in1d(np.array(["L1D_W_A = L1D_W_H","L1D_R_H","L2D_H","L3D_A","L3_H","MEM"]),counters,assume_unique=True)):
      labels_pie = ["L1D_HIT","L2D_HIT","L3_HIT","MEM"]
      
      r = self.regions[r_name]
      if(excl_or_incl == "excl"):
        values = r.get_excl_variant_values(variant_idx)[-6:,:] # cache counters are at end of array, get only these
      elif(excl_or_incl == "incl"):
        values = r.get_incl_variant_values(variant_idx)[-6:,:] # cache counters are at end of array, get only these
      val_l1d = np.clip(values[0,:] + values[1,:], 0, None)
      val_l2d = np.clip(values[2,:], 0, None)
      val_l3 = np.clip(values[4,:], 0, None)
      val_mem = np.clip(values[5,:], 0, None)
      val_all = np.array([val_l1d,val_l2d,val_l3,val_mem])
      
      if (np.count_nonzero(val_all)):
      
        fig_pie, ax_pie2 = plt.subplots(subplot_kw=dict(polar=True)) # polar plot in background to draw axes onto
        ax_pie = fig_pie.add_subplot(111,polar=False) # non-polar plot in foreground for actual ring diagram
        # draw axes
        num_axis = 16
        for i in range(num_axis):
          x = 2/num_axis * 2* i * np.pi
          ax_pie2.plot([x,x],[0,1],color="grey")
        for i in range(num_axis):
          x = 2/num_axis * (2*i + 1) * np.pi 
          ax_pie2.plot([x,x],[0,1],color="grey", linestyle='dotted')
          
        ax_pie2.xaxis.set_major_locator(mticker.MaxNLocator(8))
        ticks_loc = ax_pie2.get_xticks().tolist()[:8]
        ax_pie2.xaxis.set_major_locator(mticker.FixedLocator(ticks_loc))
#        ax_pie2.set_xticklabels(['0%','87.5%','75%','62.5%','50%','37.5%','25%','12.5%'])
        ax_pie2.set_xticklabels([])
        ax_pie2.set_yticks([])
        ax_pie2.grid(b=False,axis="x") # no x axis  
        ax_pie2.grid(b=False,axis="y") # no y axis  
        ax_pie2.set_frame_on(False) # delete outer circle of ring diagram
        
        # draw ring diagrams
        for tnum_idx,tnum in enumerate(tnums):
          vals_tnum = val_all[:,tnum_idx]
          width_rings = (1 / (len(tnums)+1))
          radius = width_rings*(1+len(tnums)) - (((len(tnums)-1)-tnum_idx)*width_rings)
          ax_pie.pie(val_all[:,tnum_idx], radius=radius,wedgeprops=dict(width=width_rings-0.005,edgecolor="w"),colors=[cmap(0.9*i / len(vals_tnum)) for i in range(1,len(vals_tnum)+1)])
        
          
        ax_pie.set(aspect="equal")
        ax_pie.set_title("C. Cache usage for region\n"+r_name+" in "+title,pad=30.0)
        # ax_pie.set_title(title+"\n"+r_name,pad=30.0)
        ax_pie.legend(labels=labels_pie,loc='upper center',bbox_to_anchor=(0.5,-0.2),ncol=2)
        # add tnum label for each ring
        ax_pie.set_xticks([((i+0.5)*width_rings) for i in range(1,len(tnums)+1)]) # set ticks to be at ring middles
        ax_pie.spines['top'].set_position('zero') # let the top axis go through (0,0)
        ax_pie.set_xticklabels(tnums, fontsize=7.5)
        ax_pie.xaxis.set_ticks_position("top") # display only ticks in top axis
        ax_pie.tick_params(length=0) # hide the tick marks, so that only the labels get displayed
        
        fig_pie.tight_layout()
        
        pdf.savefig()
        plt.close()
    
# def draw_cache_usages     (self,pdf,title,counters,width,tnums,variant_idx,base_idx,meas_idx,r_name,excl_or_incl):
  def draw_cache_usages_axes(self,ax_pie, ax_pie2, variant,counters,width,tnums,variant_idx,base_idx,meas_idx,r_name,excl_or_incl):
    cmap = plt.get_cmap("Blues")
    # check if ring diagrams should be drawn
    if np.all(np.in1d(np.array(["L1D_W_A = L1D_W_H","L1D_R_H","L2D_H","L3D_A","L3_H","MEM"]),counters,assume_unique=True)):
      labels_pie = ["L1D_HIT","L2D_HIT","L3_HIT","MEM"]
      
      r = self.regions[r_name]
      if(excl_or_incl == "excl"):
        values = r.get_excl_variant_values(variant_idx)[-6:,:] # cache counters are at end of array, get only these
      elif(excl_or_incl == "incl"):
        values = r.get_incl_variant_values(variant_idx)[-6:,:] # cache counters are at end of array, get only these
      val_l1d = np.clip(values[0,:] + values[1,:], 0, None)
      val_l2d = np.clip(values[2,:], 0, None)
      val_l3 = np.clip(values[4,:], 0, None)
      val_mem = np.clip(values[5,:], 0, None)
      val_all = np.array([val_l1d,val_l2d,val_l3,val_mem])
      
      if (np.count_nonzero(val_all)):
      
        # draw axes
        num_axis = 16
        for i in range(num_axis):
          x = 2/num_axis * 2* i * np.pi
          ax_pie2.plot([x,x],[0,1],color="grey",linewidth=.5)
        for i in range(num_axis):
          x = 2/num_axis * (2*i + 1) * np.pi 
          ax_pie2.plot([x,x],[0,1],color="grey", linestyle='dotted',linewidth=.5)
          
        ax_pie2.xaxis.set_major_locator(mticker.MaxNLocator(8))
        ticks_loc = ax_pie2.get_xticks().tolist()[:8]
        ax_pie2.xaxis.set_major_locator(mticker.FixedLocator(ticks_loc))
#        ax_pie2.set_xticklabels(['0%','87.5%','75%','62.5%','50%','37.5%','25%','12.5%'])
        ax_pie2.set_xticklabels(['','87.5%','','62.5%','','37.5%','','12.5%'])
        ax_pie2.set_yticks([])
        ax_pie2.grid(b=False,axis="x") # no x axis  
        ax_pie2.grid(b=False,axis="y") # no y axis  
        ax_pie2.set_frame_on(False) # delete outer circle of ring diagram
        
        ax_pie.set_title(variant, fontsize=LARGE_SIZE)
        # draw ring diagrams
        for tnum_idx,tnum in enumerate(tnums):
          vals_tnum = val_all[:,tnum_idx]
          width_rings = (1 / (len(tnums)+1))
          radius = width_rings*(1+len(tnums)) - (((len(tnums)-1)-tnum_idx)*width_rings)
          ax_pie.pie(val_all[:,tnum_idx], radius=radius,wedgeprops=dict(width=width_rings-0.005,edgecolor="w"),colors=[cmap(0.9*i / len(vals_tnum)) for i in range(1,len(vals_tnum)+1)])
        
          
        ax_pie.set(aspect="equal")
        ax_pie.set_xticks([((i+0.5)*width_rings) for i in range(1,len(tnums)+1)]) # set ticks to be at ring middles
        ax_pie.spines['top'].set_position('zero') # let the top axis go through (0,0)
        ax_pie.set_xticklabels(tnums, fontsize=TINY_SIZE-1)
        ax_pie.xaxis.set_ticks_position("top") # display only ticks in top axis
        ax_pie.tick_params(length=0) # hide the tick marks, so that only the labels get displayed
    
  def draw_counter_overheads(self,pdf,title,counters,width,tnums,variant_idx,base_idx,meas_idx,r_name,excl_or_incl):
    
    # draw for each region: overhead diagram for all counters
    cmap = plt.get_cmap("Blues")
    labels = counters #[counters[0],counters[2],counters[4]]
    len_tnum = len(tnums)
    x = np.arange(0,len(labels))*(len_tnum+1)*width
    
    r = self.regions[r_name]
    if (excl_or_incl == "excl"):
      overheads = r.get_excl_variant_overhead(variant_idx,base_idx)
      values_variant = r.get_excl_variant_values(variant_idx)
      values_base = r.get_excl_variant_values(base_idx)
    elif (excl_or_incl == "incl"):
      overheads = r.get_incl_variant_overhead(variant_idx,base_idx)
      values_variant = r.get_incl_variant_values(variant_idx)
      values_base = r.get_incl_variant_values(base_idx)
    else:
      print("Error: Invalid option for draw_counter_overheads, should be 'excl' or 'incl'")
    
    #overheads = np.array([overheads[0,:], overheads[2,:], overheads[4,:]])
    #values_variant = np.array([values_variant[0,:], values_variant[2,:], values_variant[4,:]])
    #values_base = np.array([values_base[0,:], values_base[2,:], values_base[4,:]])
    
    if(np.count_nonzero(values_base) and np.count_nonzero(values_variant)):
    
      fig, ax = plt.subplots()
      ax2 = ax.twinx()
      ax2_label_variant = "variant"
      ax2_label_base = "base"
      
      # cut off bars
      cutoff = calculate_cutoff(overheads)
      if (cutoff < max(np.array(overheads).flatten())):
        ax.set_ylim([0,cutoff])
        
      for tnum_idx,tnum in enumerate(tnums):
        x_offset = (tnum_idx+0.5-(0.5*len_tnum))*width
        bars = ax.bar(x + x_offset, overheads[:,tnum_idx], width, label=tnum, color=[cmap(0.9*(tnum_idx+1) / len(tnums))])
        # add cutoff flags
        if (cutoff < max(np.array(overheads).flatten())):
          add_cutoff_flags(ax,bars,cutoff,len(tnums),tnum_idx)
        
        ax2.plot(x + x_offset, values_variant[:,tnum_idx],label=ax2_label_variant, color="black",marker="x",linestyle="none",markersize=MARKER_SIZE,markeredgewidth=MARKER_WIDTH)
        ax2.plot(x + x_offset, values_base[:,tnum_idx],label=ax2_label_base, color="black",marker="+",linestyle="none",markersize=MARKER_SIZE,markeredgewidth=MARKER_WIDTH)
        #if-cases to only show the explanation of symbols once in the legend
        if (ax2_label_variant == "variant"):
          ax2_label_variant = None
        if (ax2_label_base == "base"):
          ax2_label_base = None
      
      ax.set_ylabel("Relative value")
      # ax.set_yscale("log")
      ax.set_title("A. Comparing "+title+" variant\nwith base for region "+r_name, pad = 30.0, fontdict={'fontweight' : 'bold','fontsize' : 18})
      ax.set_xticks(x)
      ax.set_xticklabels(labels, rotation=20, ha="right")
      ax.legend(loc='lower left', bbox_to_anchor=(0, 1),ncol=len(overheads[0,:]), handlelength=1)
      
      ax2.set_ylabel("Absolute values")
      ax2.set_yscale("log")
      ax2.autoscale(axis="y")
      ax2.legend(loc='lower right', bbox_to_anchor=(1, 1),ncol=2, handlelength=1)
      
      fig.tight_layout()
      
      pdf.savefig()
      plt.close()
  
  # debug function to print current data structure
  def print_data(self):
    print(self.name)
    print("Size:",self.region_size)
    
    for reg in self.regions.values():
      reg.print_data()
    

###
# class to store all data corresponding to a region (of a application in a measurement run)
#   - stores time and counter data in one DataSet (time is treated as a counter itself)
###
class Region():
  def __init__(self,name,data_size):
    self.name = name
    self.data_excl = DataSet(data_size)
    self.data_incl = DataSet(data_size)
  #
  # add values
  # 
  def add_ctr_values(self,tnum,variant,ctr_vals):
    self.data_excl.add_values(tnum,variant,ctr_vals[::2])  # all odd positions contain exclusive values
    self.data_incl.add_values(tnum,variant,ctr_vals[1::2]) # all even positions contain inclusive values
  #
  # get values
  #
  def get_excl_variant_overhead(self,variant_index,base_index):
    return self.data_excl.get_variant_overhead(variant_index,base_index)
  
  def get_excl_variant_values(self,variant_index):
    return self.data_excl.get_variant_values(variant_index)
    
  def get_incl_variant_overhead(self,variant_index,base_index):
    return self.data_incl.get_variant_overhead(variant_index,base_index)
  
  def get_incl_variant_values(self,variant_index):
    return self.data_incl.get_variant_values(variant_index)
    
  # debug function to print current data structure  
  def print_data(self):
    print(self.name)
    print("Exclusive")
    self.data_excl.print_data()
    print("Inclusive")
    self.data_incl.print_data()

###
# class to store arbitrary three-dimensional data in numpy array
### 
class DataSet():
  def __init__(self,data_size): # data_size=(#counter,#tnum,#variants)
    self.data = np.zeros(data_size)
  
  #
  # add values
  #
  def add_values(self,tnum,variant,ctr_vals):
    length = len(ctr_vals)
    self.data[:,tnum,variant] = np.array(ctr_vals)
    
  def add_value(self,ctr,tnum,variant,val):
    self.data[ctr,tnum,variant] = val
  
  #
  # get values
  #  
  def get_ctr_overhead(self,comp_index,base_index):
    return self.data[comp_index,:,:]/self.data[base_index,:,:]
    
  def get_tnum_overhead(self,comp_index,base_index):
    return self.data[:,comp_index,:]/self.data[:,base_index,:]
  
  def get_variant_overhead(self,comp_index,base_index):
    return self.data[:,:,comp_index]/self.data[:,:,base_index]
    
  def get_ctr_values(self,index):
    return self.data[index,:,:]
    
  def get_tnum_values(self,index):
    return self.data[:,index,:]
    
  def get_variant_values(self,index):
    return self.data[:,:,index]
  
  # debug function to print current data structure
  def print_data(self):
    print(self.data)
    


#########################
# FUNCTION DEFINITIONS  #
#########################

# calculate cutoff (max(double median, 90% percentil)), zero or negative values not considered)
def calculate_cutoff(values):
  # transform array in usable form
  values = np.array(values)
  values = values[np.isnan(values) == False].flatten() # get rid of nan values
  cutoff = np.inf
  if (len(values) > 0):
    values = values[values > 0] # get rid of zero values
    # calculate cutoff 
    double_median = 2 * np.median(values)
    percentile = np.percentile(values, 90)
    cutoff = max(double_median, percentile)
    
  return cutoff

# add cutoff flags for an given axis, bar graph an cutoff. nbars is the number of different bars, i_bar the momentarly bar
def add_cutoff_flags(ax,bars,cutoff,nbars,i_bar):
  # generate flags
  flagsize = MEDIUM_SIZE
  for i,bar in enumerate(bars):
    if (bar.get_height() > cutoff):
      at = ax.annotate('{:.1f}'.format(bar.get_height()),
                    fontsize=flagsize,
                    color="white",
                    xy=(bar.get_x() + bar.get_width() / 2, cutoff*.9),
                    backgroundcolor=bar.get_facecolor(),
                    xytext=(- 1, (i_bar-(nbars-1))*(flagsize*1.55)),
                    textcoords="offset points",
                    ha='right', va='bottom')
      at.get_bbox_patch().set_boxstyle("round,pad=0.2,rounding_size=0.2")


# reads config file and initializes measurement object, returns the measurement object
def read_config_file(file_pattern,path):
  
  file_name=find_all(file_pattern,path)[0]
  
  with open(file_name) as config_file:
    csv_reader = csv.reader(config_file, delimiter=',', quoting=csv.QUOTE_NONNUMERIC)
    applications = []
    tnums = []
    counters = []
    variants = []
    meas_states = []
    output_format = ""
    
    for row in csv_reader:
      row = [x.strip('"') for x in row if x != '']
      row_first = row[0].strip('"')
      if row_first == "applications":
        applications = row[1:]
      elif row_first == "tnums":
        tnums = row[1:]
      elif row_first == "counters":
        counters = row[1:]
      elif row_first == "variants":
        variants = row[1:]
      elif row_first == "meas_states":
        meas_states = row[1:]
      elif row_first == "output_format":
        output_format = row[1]
      else:
        print("Invalid config file:"+row[0]) #TODO error handling
        
  config_file.close()
  
  print("Config file read.")
  
  return Measurement(counters,tnums,variants,meas_states,output_format,applications)

    
# reads the region data and adds the data to the given measurement object
def read_region_values(measurement_obj, file_pattern, path, use_extended_name):

  files = find_all(file_pattern,path)
  
  for file_name in files:
    with open(file_name) as region_file:
      csv_reader = csv.reader(region_file, delimiter=',', quoting=csv.QUOTE_NONNUMERIC)
      
      for row in csv_reader:
        if len(row) > 0:
          if row[0] != "App": 
            measurement_obj.add_region_line(row,use_extended_name)
        
    region_file.close()
  
  print("Region values read.")
  
def find_all(file_pattern, path):
  find_string = "find " + path + " -regex '" + file_pattern + "'"
  files = [line.decode('ascii') for line in subprocess.check_output(find_string,shell=True).splitlines()] # TODO: better with subprocess.POpen to avoid decoding?
  return files


#########################
#      MAIN PROGRAM     #
#########################

#TINY_SIZE = 12
#SMALL_SIZE = 10.3
#MEDIUM_SIZE = 12
# TINY_SIZE = 10.3
# SMALL_SIZE = 10
# MEDIUM_SIZE = 11

plt.rc('font', size=MEDIUM_SIZE, weight="bold")          # controls default text sizes
plt.rc('xtick', labelsize=TINY_SIZE-1)    # fontsize of the tick labels
plt.rc('ytick', labelsize=TINY_SIZE-1)    # fontsize of the tick labels
plt.rc('legend', fontsize=SMALL_SIZE)    # legend fontsize
#plt.rc('title', fontsize=LARGE_SIZE)    # title fontsize
plt.rc('axes', labelsize=MEDIUM_SIZE, labelweight='bold', titleweight='bold',titlesize=TINY_SIZE)
plt.rcParams.update({
"font.family": "sans-serif",
'figure.autolayout': True,
})

# get path to file and how regions get named
if (len(sys.argv) >= 2):
  for arg in sys.argv[1:]:
    if(arg[:5] == "path="):
      path=arg[5:]
    elif(arg[:10] == "reg_names="):
      if(arg[10:] == "use_names"):
        use_file_and_line = False
      elif(arg[10:] == "use_lines"):
        use_file_and_line = True
      else:
        print("Error: Unknown argument for region naming. Please use 'use_names' or 'use_lines'")
        exit(1)
    else:
      print("Error: Unknown argument",arg)
      exit(1)


# read all data
meas_obj = read_config_file(".*_meta/work/output_config.csv",path)
read_region_values(meas_obj,".*_merge_results/work/output_all.csv",path,use_file_and_line)

# meas_obj.print_data()
#meas_obj.draw_region_overheads(path,"incl")
#meas_obj.draw_counter_overheads(path,"incl")
#meas_obj.draw_region_overheads(path,"excl")
#meas_obj.draw_region_overheads(path,"excl","352.nab")
#meas_obj.draw_counter_overheads(path,"excl","352.nab")

meas_obj.draw_cache_usage(path,"excl","352.nab")
#meas_obj.draw_cache_usage(path,"excl","all")
#meas_obj.draw_cache_usage(path,"excl","370.mgrid331")



  
    
    
    
    
    
    
    

