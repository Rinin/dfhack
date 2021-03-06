module DFHack
# returns an Array of all units that are current fort citizen (dwarves, on map, not hostile)
def self.unit_citizens
	race = ui.race_id
	civ = ui.civ_id
	world.units.active.find_all { |u| 
		u.race == race and u.civ_id == civ and !u.flags1.dead and !u.flags1.merchant and
		!u.flags1.diplomat and !u.flags2.resident and !u.flags3.ghostly and
		!u.curse.add_tags1.OPPOSED_TO_LIFE and !u.curse.add_tags1.CRAZED and
		u.mood != :Berserk
		# TODO check curse ; currently this should keep vampires, but may include werebeasts
	}
end

# list workers (citizen, not crazy / child / inmood / noble)
def self.unit_workers
	unit_citizens.find_all { |u|
		u.mood == :None and
		u.profession != :CHILD and
		u.profession != :BABY and
		# TODO MENIAL_WORK_EXEMPTION_SPOUSE
		!unit_entitypositions(u).find { |pos| pos.flags[:MENIAL_WORK_EXEMPTION] }
	}
end

# list currently idle workers
def self.unit_idlers
	unit_workers.find_all { |u|
		# current_job includes eat/drink/sleep/pickupequip
		!u.job.current_job._getv and 
		# filter 'attend meeting'
		u.meetings.length == 0 and
		# filter soldiers (TODO check schedule)
		u.military.squad_index == -1 and
		# filter 'on break'
		!u.status.misc_traits.find { |t| id == :OnBreak }
	}
end

def self.unit_entitypositions(unit)
	list = []
	return list if not hf = world.history.figures.binsearch(unit.hist_figure_id)
	hf.entity_links.each { |el|
		next if el._rtti_classname != :histfig_entity_link_positionst
		next if not ent = world.entities.all.binsearch(el.entity_id)
		next if not pa = ent.positions.assignments.binsearch(el.assignment_id)
		next if not pos = ent.positions.own.binsearch(pa.position_id)
		list << pos
	}
	list
end
end
