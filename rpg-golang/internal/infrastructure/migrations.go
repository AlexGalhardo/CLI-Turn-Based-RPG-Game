package infrastructure

import (
	"bytes"
	"encoding/json"
	"fmt"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

// Migrations upgrade older save/settings/history/profile documents to the current schema (docs/persistence.md).
// Each one takes the raw JSON of version N and returns version N + 1, so the application layer only ever reads the
// current format. Version 1 → 2 is the 1.4.0 "ARPG update".

const (
	removedRarity     = "epic"
	replacementRarity = "legendary"
)

type document = map[string]any

func decodeDocument(raw []byte) (document, error) {
	decoder := json.NewDecoder(bytes.NewReader(raw))
	decoder.UseNumber()

	var result document
	if err := decoder.Decode(&result); err != nil {
		return nil, fmt.Errorf("decode document: %w", err)
	}

	return result, nil
}

func schemaVersionOf(doc document) int {
	number, ok := doc["schemaVersion"].(json.Number)
	if !ok {
		return 1
	}

	version, err := number.Int64()
	if err != nil {
		return 1
	}

	return int(version)
}

func intOf(value any) int {
	number, ok := value.(json.Number)
	if !ok {
		return 0
	}

	result, _ := number.Int64()

	return int(result)
}

func setDefault(doc document, key string, value any) {
	if _, ok := doc[key]; !ok {
		doc[key] = value
	}
}

func renameRarities(items any) {
	list, ok := items.([]any)
	if !ok {
		return
	}

	for _, raw := range list {
		if item, ok := raw.(document); ok && item["rarity"] == removedRarity {
			item["rarity"] = replacementRarity
		}
	}
}

func statsV1ToV2(raw any) {
	stats, ok := raw.(document)
	if !ok {
		return
	}

	setDefault(stats, "itemsAutoEquipped", 0)
	setDefault(stats, "elitesKilled", 0)
	setDefault(stats, "potionsDropped", document{})

	if dropped, ok := stats["itemsDropped"].(document); ok {
		if epic, found := dropped[removedRarity]; found {
			delete(dropped, removedRarity)
			dropped[replacementRarity] = intOf(dropped[replacementRarity]) + intOf(epic)
		}
	}

	renameRarities(stats["droppedItems"])
}

func runV1ToV2(raw any) {
	run, ok := raw.(document)
	if !ok {
		return
	}

	if config, ok := run["config"].(document); ok {
		setDefault(config, "autoEquip", false)
	}

	setDefault(run, "won", false)

	if monster, ok := run["monster"].(document); ok {
		enemyClass := domain.EnemyNormal
		if monster["isBoss"] == true {
			enemyClass = domain.EnemyBoss
		}

		setDefault(monster, "enemyClass", string(enemyClass))
	}

	if player, ok := run["player"].(document); ok {
		renameRarities(player["bag"])

		if equipment, ok := player["equipment"].(document); ok {
			for _, item := range equipment {
				renameRarities([]any{item})
			}
		}
	}

	renameRarities(run["merchantStock"])
	statsV1ToV2(run["stats"])
}

// migrate decodes a document, upgrades it when its schema is older than 2 and encodes it again.
func migrate(raw []byte, upgrade func(document)) ([]byte, error) {
	doc, err := decodeDocument(raw)
	if err != nil {
		return nil, err
	}

	if schemaVersionOf(doc) >= 2 {
		return raw, nil
	}

	upgrade(doc)
	doc["schemaVersion"] = 2

	content, err := json.Marshal(doc)
	if err != nil {
		return nil, fmt.Errorf("encode migrated document: %w", err)
	}

	return content, nil
}

// MigrateSave upgrades save.json to the current schema.
func MigrateSave(raw []byte) ([]byte, error) {
	return migrate(raw, func(doc document) { runV1ToV2(doc["run"]) })
}

// MigrateHistory upgrades a history record to the current schema.
func MigrateHistory(raw []byte) ([]byte, error) {
	return migrate(raw, func(doc document) {
		setDefault(doc, "won", false)
		statsV1ToV2(doc["stats"])
	})
}

// MigrateProfile upgrades profile.json to the current schema.
func MigrateProfile(raw []byte) ([]byte, error) {
	return migrate(raw, func(doc document) {
		if hall, ok := doc["hallOfFame"].([]any); ok {
			for _, entry := range hall {
				if fields, ok := entry.(document); ok {
					setDefault(fields, "won", false)
				}
			}
		}
	})
}

// MigrateSettings upgrades settings.json to the current schema.
func MigrateSettings(raw []byte) ([]byte, error) {
	return migrate(raw, func(doc document) {
		setDefault(doc, "autoEquip", false)
		setDefault(doc, "battleSpeed", 1)
	})
}
